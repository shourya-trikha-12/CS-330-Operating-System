#include <stdio.h>
#include <sys/mman.h>

unsigned long *head = NULL;
unsigned long _4MB = 2048 * 2048;

unsigned long *new_chunk(unsigned long size)
{
	size += 8;
	size = (size / _4MB + 1) * _4MB;
	unsigned long *ptr;
	if ((ptr = (unsigned long *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == (unsigned long *)-1)
	{
		return NULL;
	}
	*ptr = size;
	return ptr;
}

void insert_chunk_in_freelist(unsigned long *ptr)
{
	if (head == NULL)
	{
		head = ptr;
		*(ptr + 1) = NULL; // next
		*(ptr + 2) = head; // prev
		return;
	}
	unsigned long *ptr1 = head;
	head = ptr;
	*(ptr1 + 2) = ptr;
	*(ptr + 1) = ptr1;
	*(ptr + 2) = head;
	return;
}

void remove_chunk_from_freelist(unsigned long *ptr)
{
	if (ptr == head)
	{
		unsigned long *ptr1 = *(ptr + 1);
		if (ptr1 == NULL)
		{
			head = NULL;
		}
		else
		{
			head = ptr1;
			*(ptr1 + 2) = head;
		}
	}
	else
	{
		unsigned long *ptr1, *ptr2;
		ptr1 = *(ptr + 2); // prev
		ptr2 = *(ptr + 1); // next
		if (ptr2 == NULL)
		{
			*(ptr1 + 1) = NULL;
		}
		else
		{
			*(ptr1 + 1) = ptr2;
			*(ptr2 + 2) = ptr1;
		}
	}
	return;
}

unsigned long *search_free_chunk(unsigned long size)
{
	if (head == NULL)
	{
		return NULL;
	}
	else
	{
		unsigned long *ptr = head;
		while (ptr && size + 8 > *(ptr))
		{
			ptr = *(ptr + 1);
		}
		if (!ptr)
		{
			return NULL;
		}
		return ptr;
	}
}

void *memalloc(unsigned long size)
{
	unsigned long *ptr;
	if (ptr = search_free_chunk(size))
	{
		unsigned long free_memory_chunk_size = *(ptr);
		size += 8;
		if (size % 8 != 0)
		{
			size = (size / 8 + 1) * 8;
		}
		if (size < 24)
		{
			size = 24;
		}
		unsigned long b = free_memory_chunk_size - size;
		if (b < 24)
		{
			*(ptr) = free_memory_chunk_size;
			remove_chunk_from_freelist(ptr);
			return (void *)(ptr + 1);
		}
		else
		{
			*(ptr) = size;
			remove_chunk_from_freelist(ptr);
			for (int i = 0; i < size / 8; i++)
			{
				ptr++;
			}
			*(ptr) = b;
			insert_chunk_in_freelist(ptr);
			for (int i = 0; i < size / 8; i++)
			{
				ptr--;
			}
			return (void *)(ptr + 1);
		}
	}
	else
	{
		ptr = new_chunk(size);
		unsigned long free_memory_chunk_size = *(ptr);
		size += 8;
		if (size % 8 != 0)
		{
			size = (size / 8 + 1) * 8;
		}
		if (size < 24)
		{
			size = 24;
		}
		unsigned long b = free_memory_chunk_size - size;
		if (b < 24)
		{
			return (void *)(ptr + 1);
		}
		else
		{
			*(ptr) = size;
			for (int i = 0; i < size / 8; i++)
			{
				ptr++;
			}
			*(ptr) = b;
			insert_chunk_in_freelist(ptr);
			for (int i = 0; i < size / 8; i++)
			{
				ptr--;
			}
			return (void *)(ptr + 1);
		}
	}

	return NULL;
}

int memfree(void *ptr)
{

	unsigned long size = *((unsigned long *)ptr - 1);
	unsigned long *ptr1, *ptr2;
	int a, b;
	ptr2 = ((unsigned long *)ptr + size / 8 - 1);
	unsigned long *p = head;
	while (p && p != ptr2)
	{
		p = *(p + 1);
	}
	if (!p)
	{
		b = 0; // right neighbour not free
	}
	else
	{
		b = 1; // right neighbour free
	}
	p = head;
	while (p)
	{
		unsigned long s = *(p);
		if (p + s / 8 == ptr - 1)
		{
			a = 1;
			break;
		}
		p = *(p + 1);
	}
	if (!p)
	{
		a = 0; // left neighbour not free
	}
	if (a == 1 && b == 1)
	{
		// both neighbours free
		unsigned long free_chunk_size = *(ptr1) + size + *(ptr2);
		remove_chunk_from_freelist(ptr1);
		remove_chunk_from_freelist(ptr2);
		*((unsigned long *)ptr1) = free_chunk_size;
		insert_chunk_in_freelist((unsigned long *)ptr1);
		return 0;
	}
	else if (a == 1 && b == 0)
	{
		// ptr1(left) is free
		unsigned long free_chunk_size = *(ptr1) + size;
		remove_chunk_from_freelist(ptr1);
		*((unsigned long *)ptr1) = free_chunk_size;
		insert_chunk_in_freelist((unsigned long *)ptr1);
		return 0;
	}
	else if (a == 0 && b == 1)
	{
		// ptr2(right) is free
		unsigned long free_chunk_size = *(ptr2) + size;
		remove_chunk_from_freelist(ptr2);
		*((unsigned long *)ptr) = free_chunk_size;
		insert_chunk_in_freelist((unsigned long *)ptr - 1);
		return 0;
	}
	else
	{
		// both neighbours alloted
		insert_chunk_in_freelist((unsigned long *)ptr - 1);
		return 0;
	}
	return -1;
}
