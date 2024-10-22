#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>
#define _4KB 4096

/*
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables
 *
 * */

void invalidate_page(u64 address)
{
    asm volatile("invlpg (%0)" : : "r"((u64 *)address) : "memory");
}

long search_lowest_free_region(struct vm_area *curr, u64 addr, int length, int prot, int flags)
{
    while (curr != NULL)
    {
        if (curr->vm_next == NULL)
        {
            if (curr->access_flags == prot)
            {
                curr->vm_end += length;
                return curr->vm_end - length;
            }
            else
            {
                struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                new_vma->access_flags = prot;
                new_vma->vm_start = curr->vm_end;
                new_vma->vm_end = new_vma->vm_start + length;
                new_vma->vm_next = curr->vm_next;
                curr->vm_next = new_vma;
                stats->num_vm_area++;
                return new_vma->vm_start;
            }
        }
        else if (curr->vm_next->vm_start - curr->vm_end >= length)
        {

            if (curr->access_flags == prot && curr->vm_next->vm_start - curr->vm_end > length)
            {
                curr->vm_end += length;
                return curr->vm_end - length;
            }
            else if (curr->vm_next->vm_start - curr->vm_end > length && curr->access_flags != prot)
            {
                struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                if (new_vma == NULL)
                {
                    return -EINVAL;
                }
                new_vma->access_flags = prot;
                new_vma->vm_start = curr->vm_end;
                new_vma->vm_end = new_vma->vm_start + length;
                new_vma->vm_next = curr->vm_next;
                curr->vm_next = new_vma;
                stats->num_vm_area++;
                return new_vma->vm_start;
            }
            else if (curr->access_flags == prot && curr->vm_next->vm_start - curr->vm_end == length && curr->vm_next->access_flags == prot)
            {
                u64 ans = curr->vm_end;
                struct vm_area *todel = curr->vm_next;
                curr->vm_end = curr->vm_next->vm_end;
                curr->vm_next = curr->vm_next->vm_next;
                stats->num_vm_area--;
                os_free(todel, sizeof(struct vm_area));
                return ans;
            }
            else if (curr->access_flags == prot && curr->vm_next->vm_start - curr->vm_end == length && curr->vm_next->access_flags != prot)
            {
                u64 ans = curr->vm_end;
                curr->vm_end += length;
                return ans;
            }
            else if (curr->access_flags != prot && curr->vm_next->vm_start - curr->vm_end == length && curr->vm_next->access_flags == prot)
            {
                u64 ans = curr->vm_end;
                curr->vm_next->vm_start = curr->vm_end;
                return ans;
            }
            else if (curr->access_flags != prot && curr->vm_next->vm_start - curr->vm_end == length && curr->vm_next->access_flags != prot)
            {
                struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                if (new_vma == NULL)
                {
                    return -EINVAL;
                }
                new_vma->access_flags = prot;
                new_vma->vm_start = curr->vm_end;
                new_vma->vm_end = new_vma->vm_start + length;
                new_vma->vm_next = curr->vm_next;
                curr->vm_next = new_vma;
                stats->num_vm_area++;
                return new_vma->vm_start;
            }
        }
        curr = curr->vm_next;
    }
    return -1;
}

u64 *install_page_table(u64 *prev_level_entry, u64 prev_level_offset)
{
    u64 page_table_pfn = os_pfn_alloc(OS_PT_REG);
    u64 *page_table_pointer = (u64 *)(((u64)osmap(page_table_pfn) / _4KB) * _4KB);

    prev_level_entry[prev_level_offset] = (page_table_pfn << 12) | 1LL; // present bit set to 1
    prev_level_entry[prev_level_offset] = prev_level_entry[prev_level_offset] | (1LL << 3);
    prev_level_entry[prev_level_offset] = prev_level_entry[prev_level_offset] | (1LL << 4);

    for (int i = 0; i < 512; i++)
    {
        page_table_pointer[i] = 0;
    }
    return page_table_pointer;
}

void page_table_walk(u64 current_pgd, u64 addr, u64 pfn, int access_flags)
{
    int access;
    if (access_flags == PROT_READ | PROT_WRITE)
    {
        access = 1;
    }
    else
    {
        access = 0;
    }
    u64 *pgd;
    u64 pgd_offset = (addr >> 39) & ((1 << 9) - 1);
    u64 *pud;
    u64 pud_offset = (addr >> 30) & ((1 << 9) - 1);
    u64 *pmd;
    u64 pmd_offset = (addr >> 21) & ((1 << 9) - 1);
    u64 *pte;
    u64 pte_offset = (addr >> 12) & ((1 << 9) - 1);

    pgd = (u64 *)(((u64)osmap(current_pgd) / _4KB) * _4KB);
    u64 pgd_entry = pgd[pgd_offset];
    u64 pud_pfn = pgd_entry >> 12;

    pud = (u64 *)(((u64)osmap(pud_pfn) / _4KB) * _4KB);
    if (pud == NULL)
    {
        pud = install_page_table(pgd, pgd_offset);
    }
    u64 pud_entry = pud[pud_offset];
    u64 pmd_pfn = pud_entry >> 12;

    pmd = (u64 *)(((u64)osmap(pmd_pfn) / _4KB) * _4KB);
    if (pmd == NULL)
    {
        pmd = install_page_table(pud, pud_offset);
    }
    u64 pmd_entry = pmd[pmd_offset];
    u64 pte_pfn = pmd_entry >> 12;

    pte = (u64 *)(((u64)osmap(pte_pfn) / _4KB) * _4KB);
    if (pte == NULL)
    {
        pte = install_page_table(pmd, pmd_offset);
    }
    u64 pte_entry = (pfn << 12) | 1; // present bit set to 1
    if (access)
    {
        pte_entry = pte_entry | (1 << 3); // set write bit if write access
    }
    else
    {
        u64 r = pte_entry & (1 << 3);
        if (r == (1 << 3))
        {
            pte_entry -= (1 << 3);
        }
    }
    pte_entry = pte_entry | (1 << 4);
    pte[pte_offset] = pte_entry;
    // printk("Enters page table walk\n");
    return;
}

long unmap_physical_memory(struct exec_context *current, u64 addr, u64 length)
{
    u64 page_numbers = length / _4KB;
    for (u64 i = 0; i < page_numbers; i++)
    {
        u64 vaddr = addr + i * _4KB;
        u64 *pgd;
        int pgd_offset = ((vaddr >> 39) & ((1LL << 9) - 1));
        u64 *pud;
        int pud_offset = ((vaddr >> 30) & ((1LL << 9) - 1));
        u64 *pmd;
        int pmd_offset = ((vaddr >> 21) & ((1LL << 9) - 1));
        u64 *pte;
        int pte_offset = ((vaddr >> 12) & ((1LL << 9) - 1));

        pgd = osmap(current->pgd);
        pgd = (u64 *)(_4KB * ((u64)pgd / _4KB));
        u64 pgd_entry = pgd[pgd_offset];
        if ((pgd_entry & 1LL) == 0)
        { 
            continue;
        }

        pud = osmap(pgd_entry >> 12);
        pud = (u64 *)(_4KB * ((u64)pud / _4KB));
        u64 pud_entry = pud[pud_offset];
        if ((pud_entry & 1LL) == 0)
        { 
            continue;
        }

        pmd = osmap(pud_entry >> 12);
        pmd = (u64 *)(_4KB * ((u64)pmd / _4KB));
        u64 pmd_entry = pmd[pmd_offset];
        if ((pmd_entry & 1LL) == 0)
        { 
            continue;
        }

        pte = osmap(pmd_entry >> 12);
        pte = (u64 *)(_4KB * ((u64)pte / _4KB));
        u64 pte_entry = pte[pte_offset];
        if ((pte_entry & 1LL) == 0)
        { 
            continue;
        }

        u64 pfn = (pte_entry >> 12);

        invalidate_page(vaddr);
        put_pfn(pfn);
        u64 ref_count = get_pfn_refcount(pfn);
        if (ref_count == 0)
        {
            os_pfn_free(USER_REG, pfn);
        }
        pte[pte_offset] = 0;
    }
    return 1;
}

long page_table_protect(struct exec_context *current, u64 addr, u64 length, u64 access)
{
    int page_numbers = length / _4KB;
    for (int i = 0; i < page_numbers; i++)
    {
        // u64 pfn = os_pfn_alloc(USER_REG);
        u64 vaddr = addr + i * _4KB;
        u64 *pgd;
        u64 pgd_offset = (vaddr >> 39) & ((1LL << 9) - 1);
        u64 *pud;
        u64 pud_offset = (vaddr >> 30) & ((1LL << 9) - 1);
        u64 *pmd;
        u64 pmd_offset = (vaddr >> 21) & ((1LL << 9) - 1);
        u64 *pte;
        u64 pte_offset = (vaddr >> 12) & ((1LL << 9) - 1);

        pgd = (u64 *)(((u64)osmap(current->pgd) / _4KB) * _4KB);
        u64 pgd_entry = pgd[pgd_offset];
        if ((pgd_entry & 1LL) == 0)
        {
            continue;
        }
        u64 pud_pfn = pgd_entry >> 12;

        pud = (u64 *)(((u64)osmap(pud_pfn) / _4KB) * _4KB);
        u64 pud_entry = pud[pud_offset];
        if ((pud_entry & 1LL) == 0)
        {
            continue;
        }
        u64 pmd_pfn = pud_entry >> 12;

        pmd = (u64 *)(((u64)osmap(pmd_pfn) / _4KB) * _4KB);
        u64 pmd_entry = pmd[pmd_offset];
        if ((pmd_entry & 1LL) == 0)
        {
            continue;
        }
        u64 pte_pfn = pmd_entry >> 12;

        pte = (u64 *)(((u64)osmap(pte_pfn) / _4KB) * _4KB);
        u64 pte_entry = pte[pte_offset];
        if ((pte_entry & 1LL) == 0)
        {
            continue;
        }
        else
        {
            u64 s = get_pfn_refcount(pte_entry >> 12);
            if (s > 1)
            {
                return 1;
            }
            else if (s == 1)
            {
                if (access)
                {
                    pte[pte_offset] = pte[pte_offset] | (1LL << 3);
                }
                else
                {
                    u64 r = pte[pte_offset] & (1LL << 3);
                    if (r == (1LL << 3))
                    {
                        pte[pte_offset] -= (1LL << 3); // remove write access
                    }
                }
                invalidate_page(vaddr);
            }
            else
            {
                return -EINVAL;
            }
        }
        // u64 pte_entry = (pfn << 12) | 1; // present bit set to 1
        // if (access)
        // {
        //     pte_entry = pte_entry | (1 << 3); // set write bit if write access
        // }
        // pte_entry = pte_entry | (1 << 4);
        // pte[pte_offset] = pte_entry;
    }
    return 1;
}

long vm_area_unmap_2(struct exec_context *current, u64 addr, int length)
{
    length = (length / _4KB + (length % _4KB != 0)) * _4KB;
    struct vm_area *curr = current->vm_area, *prev;

    while (curr != NULL)
    {
        u64 start = curr->vm_start;
        u64 end = curr->vm_end - 1;
        if (curr->access_flags == 0x0)
        {
            prev = curr;
            curr = curr->vm_next;
            continue;
        }

        if (addr > end || addr + length - 1 < start)
        {
            prev = curr;
            curr = curr->vm_next;
            continue;
        }
        if (addr <= start && addr + length - 1 >= end)
        {
            struct vm_area *todel = curr;
            prev->vm_next = curr->vm_next;
            curr = curr->vm_next;
            os_free(todel, sizeof(struct vm_area));
            stats->num_vm_area--;
        }
        else if (addr > start && addr <= end && addr + length - 1 >= end)
        {
            curr->vm_end = addr;
            prev = curr;
            curr = curr->vm_next;
        }
        else if (addr > start && addr < end && addr + length - 1 > start && addr + length - 1 < end)
        {
            struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
            if (new_vma == NULL)
            {
                return -EINVAL;
            }
            new_vma->access_flags = curr->access_flags;
            new_vma->vm_start = addr + length;
            new_vma->vm_end = curr->vm_end;
            new_vma->vm_next = curr->vm_next;
            curr->vm_end = addr;
            curr->vm_next = new_vma;
            stats->num_vm_area++;
            return 0;
        }
        else if (addr <= start && addr + length - 1 >= start && addr + length - 1 < end)
        {
            curr->vm_start = addr + length;
            prev = curr;
            curr = curr->vm_next;
            return 0;
        }
    }
    return 0;
}

void create_page_table_entries(u64 start_addr, u64 end_addr, u64 src_pgd_pfn, u64 dest_pgd_pfn)
{

    for (u64 addr = start_addr; addr != end_addr; addr += _4KB)
    {
        u64 *dest_pgd;
        u64 *src_pgd;
        int pgd_offset = ((addr >> 39) & ((1LL << 9) - 1));
        u64 *dest_pud;
        u64 *src_pud;
        int pud_offset = ((addr >> 30) & ((1LL << 9) - 1));
        u64 *dest_pmd;
        u64 *src_pmd;
        int pmd_offset = ((addr >> 21) & ((1LL << 9) - 1));
        u64 *dest_pte;
        u64 *src_pte;
        int pte_offset = ((addr >> 12) & ((1LL << 9) - 1));

        src_pgd = osmap(src_pgd_pfn);
        src_pgd = (u64 *)(_4KB * ((u64)src_pgd / _4KB));
        dest_pgd = osmap(dest_pgd_pfn);
        dest_pgd = (u64 *)(_4KB * ((u64)dest_pgd / _4KB));
        u64 src_pgd_entry = src_pgd[pgd_offset];
        u64 dest_pgd_entry = dest_pgd[pgd_offset];
        if ((src_pgd_entry & 1LL) == 0)
        {
            continue;
        }
        if ((dest_pgd_entry & 1LL) == 0)
        {
            install_page_table(dest_pgd, pgd_offset);
        }

        src_pud = osmap(src_pgd[pgd_offset] >> 12);
        src_pud = (u64 *)(_4KB * ((u64)src_pud / _4KB));
        dest_pud = osmap(dest_pgd[pgd_offset] >> 12);
        dest_pud = (u64 *)(_4KB * ((u64)dest_pud / _4KB));
        u64 src_pud_entry = src_pud[pud_offset];
        u64 dest_pud_entry = dest_pud[pud_offset];
        if ((src_pud_entry & 1LL) == 0)
        {
            continue;
        }
        if ((dest_pud_entry & 1LL) == 0)
        {
            install_page_table(dest_pud, pud_offset);
        }

        src_pmd = osmap(src_pud[pud_offset] >> 12);
        src_pmd = (u64 *)(_4KB * ((u64)src_pmd / _4KB));
        dest_pmd = osmap(dest_pud[pud_offset] >> 12);
        dest_pmd = (u64 *)(_4KB * ((u64)dest_pmd / _4KB));
        u64 src_pmd_entry = src_pmd[pmd_offset];
        u64 dest_pmd_entry = dest_pmd[pmd_offset];
        if ((src_pmd_entry & 1LL) == 0)
        {
            continue;
        }
        if ((dest_pmd_entry & 1LL) == 0)
        {
            install_page_table(dest_pmd, pmd_offset);
        }

        src_pte = osmap(src_pmd[pmd_offset] >> 12);
        src_pte = (u64 *)(_4KB * ((u64)src_pte / _4KB));
        dest_pte = osmap(dest_pmd[pmd_offset] >> 12);
        dest_pte = (u64 *)(_4KB * ((u64)dest_pte / _4KB));
        u64 src_pte_entry = src_pte[pte_offset];
        u64 dest_pte_entry = dest_pte[pte_offset];
        if ((src_pte_entry & 1LL) == 0)
        {
            continue;
        }

        get_pfn((src_pte[pte_offset]) >> 12);
        u64 r = src_pte[pte_offset] & (1LL << 3);
        if (r == (1LL << 3))
        {
            src_pte[pte_offset] -= (1LL << 3);
        }
        dest_pte[pte_offset] = src_pte[pte_offset];
    }
}

long update_page_table_on_CoW_fault(struct exec_context *current, u64 vaddr)
{
    u64 *pgd;
    int pgd_offset = ((vaddr >> 39) & ((1LL << 9) - 1));
    u64 *pud;
    int pud_offset = ((vaddr >> 30) & ((1LL << 9) - 1));
    u64 *pmd;
    int pmd_offset = ((vaddr >> 21) & ((1LL << 9) - 1));
    u64 *pte;
    int pte_offset = ((vaddr >> 12) & ((1LL << 9) - 1));

    pgd = osmap(current->pgd);
    pgd = (u64 *)(_4KB * ((u64)pgd / _4KB));
    u64 pgd_entry = pgd[pgd_offset];
    if ((pgd_entry & 1LL) == 0)
    {
        return 1;
    }

    pud = osmap(pgd_entry >> 12);
    pud = (u64 *)(_4KB * ((u64)pud / _4KB));
    u64 pud_entry = pud[pud_offset];
    if ((pud_entry & 1LL) == 0)
    {
        return 1;
    }

    pmd = osmap(pud_entry >> 12);
    pmd = (u64 *)(_4KB * ((u64)pmd / _4KB));
    u64 pmd_entry = pmd[pmd_offset];
    if ((pmd_entry & 1LL) == 0)
    {
        return 1;
    }

    pte = osmap(pmd_entry >> 12);
    pte = (u64 *)(_4KB * ((u64)pte / _4KB));
    u64 pte_entry = pte[pte_offset];
    if ((pte_entry & 1LL) == 0)
    {
        return 1;
    }

    u64 curr_pfn = (pte_entry >> 12);
    if (get_pfn_refcount(curr_pfn) > 1)
    {
        u64 new_pfn = os_pfn_alloc(USER_REG);
        if (new_pfn == 0)
            return -1;

        u64 *new_pfn_vaddr = osmap(new_pfn);
        new_pfn_vaddr = (u64 *)(_4KB * ((u64)new_pfn_vaddr / _4KB));
        u64 *curr_pfn_vaddr = osmap(curr_pfn);
        curr_pfn_vaddr = (u64 *)(_4KB * ((u64)curr_pfn_vaddr / _4KB));
        memcpy((char *)new_pfn_vaddr, (char *)curr_pfn_vaddr, _4KB);
        pte[pte_offset] = (new_pfn << 12) | 1LL;
        pte[pte_offset] |= (1LL << 3);
        pte[pte_offset] |= (1LL << 4);
        put_pfn(curr_pfn);
    }
    pte[pte_offset] |= (1LL << 3); // enable write access
    // invalidate_page(vaddr);
    return 1;
}

/**
 * mprotect System call Implementation.
 */
long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE))
    {
        return -EINVAL;
    }
    length = (length / _4KB + ((length % _4KB) != 0)) * _4KB;
    u64 start_addr = addr;
    u64 end_addr = addr + length - 1;
    u64 start_point, region_length;
    struct vm_area *curr = current->vm_area;
    curr = curr->vm_next;

    while (curr != NULL)
    {
        u64 start = curr->vm_start;
        u64 end = curr->vm_end - 1;
        if (end < start_addr || end_addr < start)
        {
            curr = curr->vm_next;
            // printk("1 case\n");
            continue;
        }
        if (start_addr <= start && end_addr >= end)
        {
            // start => end
            start_point = start;
            region_length = end - start + 1;
            // printk("2 case\n");
        }
        else if (start_addr > start && end_addr < end)
        {
            // start_addr => end_addr
            start_point = start_addr;
            region_length = length;
            // printk("3 case\n");
        }
        else if (end_addr > start && end_addr < end)
        {
            // start => end_addr
            start_point = start;
            region_length = end_addr - start + 1;
            // printk("4 case\n");
        }
        else if (start_addr > start && start_addr < end)
        {
            // start_addr => end
            start_point = start_addr;
            region_length = end - start_addr + 1;
            // printk("5 case\n");
        }

        page_table_protect(current, start_point, region_length, prot / 2);
        // printk("after page table protect \n");
        if (vm_area_unmap_2(current, start_point, region_length) == -EINVAL)
        {
            return -EINVAL;
        }
        // printk("unmapped successfully\n");
        if (vm_area_map(current, start_point, region_length, prot, MAP_FIXED) == -EINVAL)
        {
            return -EINVAL;
        }
        curr = curr->vm_next;
    }
    return 0;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    if (flags != MAP_FIXED && flags != 0)
    {
        return -EINVAL;
    }
    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE))
    {
        return -EINVAL;
    }
    length = (length / _4KB + ((length % _4KB) != 0)) * _4KB;
    if (current->vm_area == NULL)
    {
        struct vm_area *dummy = os_alloc(sizeof(struct vm_area));
        if (dummy == NULL)
        {
            return -EINVAL;
        }
        dummy->vm_start = MMAP_AREA_START;
        dummy->access_flags = 0x0;
        dummy->vm_end = MMAP_AREA_START + _4KB;
        dummy->vm_next = NULL;
        current->vm_area = dummy;
        stats->num_vm_area++;
    }

    if ((u64 *)addr == NULL && flags == MAP_FIXED)
    {
        return -EINVAL;
    }
    else if ((u64 *)addr == NULL && flags == 0)
    {
        // no hint address
        // search for lowest free region
        struct vm_area *curr = current->vm_area;
        long ans = search_lowest_free_region(curr, addr, length, prot, flags);
        if (ans < 0)
            return -EINVAL;
        return ans;
    }
    else
    {
        // hint address
        // first search in the VMAs list, if requested region free then allocate
        if (addr < MMAP_AREA_START || addr > MMAP_AREA_END)
        {
            return -EINVAL;
        }
        struct vm_area *curr = current->vm_area;
        int overlap = 0;

        while (curr != NULL)
        {
            u64 start = curr->vm_start;
            u64 end = curr->vm_end - 1;
            if ((addr >= start && addr <= end) || (addr + length - 1 >= start && addr + length - 1 <= end) || (addr <= start && addr + length - 1 >= end))
            {
                overlap = 1;
                break;
            }
            curr = curr->vm_next;
        }
        if (overlap && flags == MAP_FIXED)
        {
            return -EINVAL;
        }
        else if (overlap && flags == 0)
        { // search for lowest free region
            curr = current->vm_area;
            long ans = search_lowest_free_region(curr, addr, length, prot, flags);
            if (ans < 0)
                return -EINVAL;
            return ans;
        }
        else
        {
            curr = current->vm_area;
            while (curr->vm_next != NULL)
            {
                if (addr >= curr->vm_end && addr + length <= curr->vm_next->vm_start)
                {
                    break;
                }
                curr = curr->vm_next;
            }

            if (curr->vm_next == NULL)
            {
                // insert at last
                u64 ans = addr;
                if (curr->vm_end == addr && curr->access_flags == prot)
                {
                    curr->vm_end = addr + length;
                }
                else
                {
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (new_vma == NULL)
                    {
                        return -EINVAL;
                    }
                    new_vma->access_flags = prot;
                    new_vma->vm_start = addr;
                    new_vma->vm_end = new_vma->vm_start + length;
                    new_vma->vm_next = curr->vm_next;
                    curr->vm_next = new_vma;
                    stats->num_vm_area++;
                }
                return ans;
            }
            else
            {
                // 9 cases
                if (addr > curr->vm_end && addr + length < curr->vm_next->vm_start)
                {
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (new_vma == NULL)
                    {
                        return -EINVAL;
                    }
                    new_vma->access_flags = prot;
                    new_vma->vm_start = addr;
                    new_vma->vm_end = new_vma->vm_start + length;
                    new_vma->vm_next = curr->vm_next;
                    curr->vm_next = new_vma;
                    stats->num_vm_area++;
                }
                else if (addr == curr->vm_end && addr + length < curr->vm_next->vm_start && curr->access_flags == prot)
                {
                    curr->vm_end += length;
                }
                else if (addr == curr->vm_end && addr + length < curr->vm_next->vm_start && curr->access_flags != prot)
                {
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (new_vma == NULL)
                    {
                        return -EINVAL;
                    }
                    new_vma->access_flags = prot;
                    new_vma->vm_start = addr;
                    new_vma->vm_end = new_vma->vm_start + length;
                    new_vma->vm_next = curr->vm_next;
                    curr->vm_next = new_vma;
                    stats->num_vm_area++;
                }
                else if (addr > curr->vm_end && addr + length < curr->vm_next->vm_start && curr->vm_next->access_flags == prot)
                {
                    curr->vm_next->vm_start = addr;
                }
                else if (addr > curr->vm_end && addr + length == curr->vm_next->vm_start && curr->vm_next->access_flags != prot)
                {
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (new_vma == NULL)
                    {
                        return -EINVAL;
                    }
                    new_vma->access_flags = prot;
                    new_vma->vm_start = addr;
                    new_vma->vm_end = new_vma->vm_start + length;
                    new_vma->vm_next = curr->vm_next;
                    curr->vm_next = new_vma;
                    stats->num_vm_area++;
                }
                else if (addr == curr->vm_end && addr + length == curr->vm_next->vm_start && curr->access_flags == prot && curr->vm_next->access_flags == prot)
                {
                    struct vm_area *todel = curr->vm_next;
                    curr->vm_end = curr->vm_next->vm_end;
                    curr->vm_next = curr->vm_next->vm_next;
                    stats->num_vm_area--;
                    os_free(todel, sizeof(struct vm_area));
                }
                else if (addr == curr->vm_end && addr + length == curr->vm_next->vm_start && curr->access_flags != prot && curr->vm_next->access_flags == prot)
                {
                    curr->vm_next->vm_start = addr;
                }
                else if (addr == curr->vm_end && addr + length == curr->vm_next->vm_start && curr->access_flags == prot && curr->vm_next->access_flags != prot)
                {
                    curr->vm_end += length;
                }
                else if (addr == curr->vm_end && addr + length == curr->vm_next->vm_start && curr->access_flags != prot && curr->vm_next->access_flags != prot)
                {
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (new_vma == NULL)
                    {
                        return -EINVAL;
                    }
                    new_vma->access_flags = prot;
                    new_vma->vm_start = addr;
                    new_vma->vm_end = new_vma->vm_start + length;
                    new_vma->vm_next = curr->vm_next;
                    curr->vm_next = new_vma;
                    stats->num_vm_area++;
                }
                return addr;
            }
        }
    }
    return -EINVAL;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    length = (length / _4KB + ((length % _4KB) != 0)) * _4KB;
    struct vm_area *curr = current->vm_area, *prev;
    // curr = curr->vm_next;

    while (curr != NULL)
    {
        u64 start = curr->vm_start;
        u64 end = curr->vm_end - 1;
        if (curr->access_flags == 0x0)
        {
            prev = curr;
            curr = curr->vm_next;
            continue;
        }

        if (addr > end || addr + length - 1 < start)
        {
            prev = curr;
            curr = curr->vm_next;
            continue;
        }
        if (addr <= start && addr + length - 1 >= end)
        {
            struct vm_area *todel = curr;
            prev->vm_next = curr->vm_next;
            curr = curr->vm_next;
            os_free(todel, sizeof(struct vm_area));
            stats->num_vm_area--;
            if (unmap_physical_memory(current, start, end - start + 1) < 0)
            {
                return -EINVAL;
            }
        }
        else if (addr > start && addr <= end && addr + length - 1 >= end)
        {
            curr->vm_end = addr;
            prev = curr;
            curr = curr->vm_next;
            if (unmap_physical_memory(current, addr, end - addr + 1) < 0)
            {
                return -EINVAL;
            }
        }
        else if (addr > start && addr < end && addr + length - 1 > start && addr + length - 1 < end)
        {
            struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
            if (new_vma == NULL)
            {
                return -EINVAL;
            }
            new_vma->access_flags = curr->access_flags;
            new_vma->vm_start = addr + length;
            new_vma->vm_end = curr->vm_end;
            new_vma->vm_next = curr->vm_next;
            curr->vm_end = addr;
            curr->vm_next = new_vma;
            stats->num_vm_area++;
            if (unmap_physical_memory(current, addr, length) < 0)
            {
                return -EINVAL;
            }
            return 0;
        }
        else if (addr <= start && addr + length - 1 >= start && addr + length - 1 < end)
        {
            curr->vm_start = addr + length;
            prev = curr;
            curr = curr->vm_next;
            if (unmap_physical_memory(current, start, addr + length - start) < 0)
            {
                return -EINVAL;
            }
            return 0;
        }
    }
    return 0;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    struct vm_area *curr = current->vm_area;
    int valid = 0;
    int access = error_code & (1 << 1);
    int cpl = error_code & (1 << 2);

    while (curr != NULL)
    {
        if (addr >= curr->vm_start && addr < curr->vm_end)
        {
            if ((access && (curr->access_flags & (1 << 1))) || (!access && (curr->access_flags & (1 << 0))))
            {
                valid = 1;
                break;
            }
        }
        curr = curr->vm_next;
    }
    if (!valid)
    {
        return -EINVAL;
    }
    if (error_code == 0x4 || error_code == 0x6)
    {
        u64 pfn = os_pfn_alloc(USER_REG);
        page_table_walk(current->pgd, addr, pfn, curr->access_flags); // update page table entries
    }
    else if (error_code == 0x7)
    {
        if (curr->access_flags & (1 << 1))
        {
            if (handle_cow_fault(current, addr, curr->access_flags) < 0)
            {
                return -EINVAL;
            }
        }
        else
        {
            return -EINVAL;
        }
    }

    return 1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the
 * end of this function (e.g., setup_child_context etc.)
 */

long copy_VM_areas_from_parent_to_child(struct exec_context *ctx, struct exec_context *new_ctx)
{
    if (ctx == NULL)
    {
        return -EINVAL;
    }
    struct vm_area *curr = ctx->vm_area;
    struct vm_area *prev = (struct vm_area *)os_alloc(sizeof(struct vm_area));
    if (prev == NULL)
    {
        return -EINVAL;
    }
    stats->num_vm_area++;
    prev->vm_start = MMAP_AREA_START;
    prev->access_flags = 0x0;
    prev->vm_end = MMAP_AREA_START + _4KB;
    prev->vm_next = NULL;
    new_ctx->vm_area = prev;
    curr = curr->vm_next;
    while (curr != NULL)
    {
        struct vm_area *temp = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        stats->num_vm_area++;
        temp->access_flags = curr->access_flags;
        temp->vm_end = curr->vm_end;
        temp->vm_start = curr->vm_start;
        prev->vm_next = temp;
        curr = curr->vm_next;
        prev = temp;
    }
    prev->vm_next = NULL;
    return 1;
}

long do_cfork()
{
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    /* Do not modify above lines
     *
     * */
    /*--------------------- Your code [start]---------------*/

    new_ctx->alarm_config_time = ctx->alarm_config_time;
    new_ctx->ctx_threads = ctx->ctx_threads;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        new_ctx->files[i] = ctx->files[i];
    }
    for (int i = 0; i < MAX_MM_SEGS; i++)
    {
        new_ctx->mms[i] = ctx->mms[i];
    }
    for (int i = 0; i < 64; i++)
    {
        new_ctx->name[i] = ctx->name[i];
    }
    new_ctx->os_rsp = ctx->os_rsp;
    // new_ctx->os_stack_pfn = ctx->os_stack_pfn;
    new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;
    pid = new_ctx->pid;
    new_ctx->ppid = ctx->pid;
    new_ctx->regs = ctx->regs;
    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        new_ctx->sighandlers[i] = ctx->sighandlers[i];
    }
    new_ctx->state = ctx->state;
    new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;
    new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;
    new_ctx->type = ctx->type;
    new_ctx->used_mem = ctx->used_mem;

    if (copy_VM_areas_from_parent_to_child(ctx, new_ctx) < 0)
    {
        return -EINVAL;
    }

    new_ctx->pgd = os_pfn_alloc(OS_PT_REG);
    if (new_ctx->pgd == 0)
    {
        return -EINVAL;
    }
    u64 *new_pgd = (u64 *)(((u64)osmap(new_ctx->pgd) / _4KB) * _4KB);
    for (int i = 0; i < 512; i++)
    {
        new_pgd[i] = 0;
    }

    struct vm_area *curr = ctx->vm_area;
    while (curr != NULL)
    {
        create_page_table_entries(curr->vm_start, curr->vm_end, ctx->pgd, new_ctx->pgd);
        curr = curr->vm_next;
    }

    for (int i = 0; i < MAX_MM_SEGS; i++)
    {
        if (i != MM_SEG_STACK)
        {
            create_page_table_entries(ctx->mms[i].start, ctx->mms[i].next_free, ctx->pgd, new_ctx->pgd);
        }
        else
        {
            create_page_table_entries(ctx->mms[i].start, ctx->mms[i].end, ctx->pgd, new_ctx->pgd);
        }
    }

    /*--------------------- Your code [end] ----------------*/

    /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}

/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data)
 * it is called when there is a CoW violation in these areas.
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    if ((PROT_WRITE & access_flags) == 0)
    {
        return -EINVAL;
    }

    if (update_page_table_on_CoW_fault(current, vaddr) < 0)
    {
        return -EINVAL;
    }

    return 1;
}
