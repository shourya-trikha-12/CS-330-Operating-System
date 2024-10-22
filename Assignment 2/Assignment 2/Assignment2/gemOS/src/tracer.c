#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>
#include <tracer.h>

///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

int is_valid_mem_range(unsigned long buff, u32 count, int access_bit)
{
	struct exec_context *ctx = get_current_ctx();

	for (int i = 0; i < 3; i++)
	{
		if (buff >= ctx->mms[i].start && buff + count <= ctx->mms[i].next_free)
		{
			if (access_bit & O_WRITE)
			{
				if (ctx->mms[i].access_flags & O_WRITE)
				{
					return 1;
				}
				else
				{
					return 0;
				}
			}
			if (access_bit & O_READ)
			{
				if ((ctx->mms[i].access_flags) & O_READ)
				{
					return 1;
				}
				else
				{
					return 0;
				}
			}
		}
	}
	if (buff >= ctx->mms[3].start && buff + count <= ctx->mms[3].end)
	{
		if (access_bit & O_WRITE)
		{
			if (ctx->mms[3].access_flags & O_WRITE)
			{
				return 1;
			}
			else
			{
				return 0;
			}
		}
		if (access_bit & O_READ)
		{
			if ((ctx->mms[3].access_flags) & O_READ)
			{
				return 1;
			}
			else
			{
				return 0;
			}
		}
	}

	struct vm_area *p = ctx->vm_area;
	while (p != NULL)
	{
		if (buff >= p->vm_start && buff + count <= p->vm_end)
		{
			if (access_bit & O_WRITE)
			{
				if (p->access_flags & O_WRITE)
				{
					return 1;
				}
				else
				{
					return 0;
				}
			}
			if (access_bit & O_READ)
			{
				if (p->access_flags & O_READ)
				{
					return 1;
				}
				else
				{
					return 0;
				}
			}
		}
		p = p->vm_next;
	}

	return 0;
}

long trace_buffer_close(struct file *filep)
{
	filep->ref_count--;
	if (filep->ref_count == 0)
	{
		if (filep == NULL)
		{
			return -EINVAL;
		}
		if (filep->type != TRACE_BUFFER)
		{
			return -EINVAL;
		}
		if (filep->trace_buffer->buffer)
		{
			os_page_free(USER_REG, filep->trace_buffer->buffer);
		}
		if (filep->trace_buffer)
		{
			os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));
		}
		if (filep->fops)
		{
			os_free(filep->fops, sizeof(struct fileops));
		}
		os_free(filep, sizeof(struct file));
	}

	return 0;
}

int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	if (!(filep->mode & O_READ))
	{
		return -EINVAL;
	}
	if (is_valid_mem_range((unsigned long)buff, count, O_WRITE) != 1)
	{
		return -EBADMEM;
	}
	int R = filep->trace_buffer->readOffset;
	int W = filep->trace_buffer->writeOffset;
	char *trace_buff = (char *)filep->trace_buffer->buffer;
	int read_capacity = (4096 + W - R) % 4096;
	if (read_capacity == 0 && filep->trace_buffer->isFull)
	{
		read_capacity = 4096;
	}
	else if (read_capacity == 0 && !filep->trace_buffer->isFull)
	{
		return 0;
	}
	int i;
	for (i = 0; i < count && i < read_capacity; i++)
	{
		buff[i] = trace_buff[R];
		R = (R + 1) % 4096;
	}
	filep->trace_buffer->readOffset = R;
	if (i == read_capacity)
	{
		filep->trace_buffer->isFull = 0;
	}
	return i;
}

int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	if (!(filep->mode & O_WRITE))
	{
		return -EINVAL;
	}

	if (is_valid_mem_range((unsigned long)buff, count, O_READ) != 1)
	{
		return -EBADMEM;
	}

	int R = filep->trace_buffer->readOffset;
	int W = filep->trace_buffer->writeOffset;
	char *trace_buff = (char *)filep->trace_buffer->buffer;
	int write_capacity = (4096 + R - W) % 4096;
	if (write_capacity == 0 && filep->trace_buffer->isFull)
	{
		return 0;
	}
	else if (write_capacity == 0 && !(filep->trace_buffer->isFull))
	{
		write_capacity = 4096;
	}

	int i;
	for (i = 0; i < count && i < write_capacity; i++)
	{
		trace_buff[W] = buff[i];
		W = (W + 1) % 4096;
	}
	filep->trace_buffer->writeOffset = W;
	if (i == write_capacity)
	{
		filep->trace_buffer->isFull = 1;
	}
	return i;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	int i;
	if (current == NULL)
	{
		return -EINVAL;
	}
	if (mode < 0 || mode > 7)
	{
		return -EINVAL;
	}
	for (i = 0; i < MAX_OPEN_FILES; i++)
	{
		if (current->files[i] == NULL)
		{
			break;
		}
	}
	if (i == MAX_OPEN_FILES)
	{
		return -EINVAL;
	}

	struct file *new_file = os_alloc(sizeof(struct file));
	if (new_file == NULL)
	{
		return -ENOMEM;
	}

	new_file->type = TRACE_BUFFER;
	new_file->mode = mode;
	new_file->offp = 0; // Latest read or write offset
	new_file->ref_count = 1;
	new_file->inode = NULL;

	new_file->trace_buffer = os_alloc(sizeof(struct trace_buffer_info));
	if (new_file->trace_buffer == NULL)
	{
		os_free(new_file, sizeof(struct file)); // Free the previously allocated memory.
		return -ENOMEM;
	}

	new_file->trace_buffer->readOffset = 0;
	new_file->trace_buffer->writeOffset = 0;
	new_file->trace_buffer->isFull = 0;
	new_file->trace_buffer->buffer = os_page_alloc(USER_REG);
	if (new_file->trace_buffer->buffer == NULL)
	{
		// Memory allocation failure.
		os_free(new_file->trace_buffer, sizeof(struct trace_buffer_info)); // Free the allocated trace buffer.
		os_free(new_file, sizeof(struct file));							   // Free the allocated file.
		return -ENOMEM;
	}

	// Allocate and initialize the fileops object.
	struct fileops *new_fops = os_alloc(sizeof(struct fileops));
	if (new_fops == NULL)
	{
		os_page_free(USER_REG, new_file->trace_buffer->buffer);			   // Free the allocated buffer.
		os_free(new_file->trace_buffer, sizeof(struct trace_buffer_info)); // Free the allocated trace buffer.
		os_free(new_file, sizeof(struct file));							   // Free the allocated file.
		return -ENOMEM;
	}

	new_fops->read = trace_buffer_read;	  // Assign the appropriate read function.
	new_fops->write = trace_buffer_write; // Assign the appropriate write function.
	new_fops->lseek = NULL;				  // Implement lseek as required.
	new_fops->close = trace_buffer_close; // Implement close as required.

	// Assign the fileops object to the file.
	new_file->fops = new_fops;

	// Finally, return the file descriptor to the user.
	current->files[i] = new_file;
	// printk("Create Trace buffer done\n");
	return i;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	struct exec_context *current = get_current_ctx();
	if (current == NULL)
	{
		return -EINVAL;
	}
	if (syscall_num == SYSCALL_START_STRACE || syscall_num == SYSCALL_END_STRACE)
	{
		return 0;
	}
	int fd = current->st_md_base->strace_fd;
	int mode = current->st_md_base->tracing_mode;
	struct file *trace_buffer_file = current->files[fd];
	struct trace_buffer_info *curr_trace_buffer = trace_buffer_file->trace_buffer;
	int num_of_arguements = 0;
	if (current->st_md_base->is_traced == 0)
	{
		return 0;
	}

	if (syscall_num == 2 || syscall_num == 10 || syscall_num == 11 || syscall_num == 13 || syscall_num == 15 || syscall_num == 20 || syscall_num == 21 || syscall_num == 22 || syscall_num == 38)
	{
		// no arguements
		num_of_arguements = 0;
	}
	else if (syscall_num == 1 || syscall_num == 6 || syscall_num == 7 || syscall_num == 12 || syscall_num == 14 || syscall_num == 19 || syscall_num == 27 || syscall_num == 29 || syscall_num == 36)
	{
		// 1 arguement
		num_of_arguements = 1;
	}
	else if (syscall_num == 4 || syscall_num == 8 || syscall_num == 9 || syscall_num == 17 || syscall_num == 23 || syscall_num == 28 || syscall_num == 37 || syscall_num == 40)
	{
		// 2 arguements
		num_of_arguements = 2;
	}
	else if (syscall_num == 5 || syscall_num == 18 || syscall_num == 24 || syscall_num == 25 || syscall_num == 30 || syscall_num == 39 || syscall_num == 41)
	{
		// 3 arguements
		num_of_arguements = 3;
	}
	else if (syscall_num == 16 || syscall_num == 35)
	{
		// 4 arguements
		num_of_arguements = 4;
	}
	else
	{
		return -EINVAL;
	}
	char *buff = (char *)curr_trace_buffer->buffer;
	int readOff = curr_trace_buffer->readOffset;
	int writeOff = curr_trace_buffer->writeOffset;
	if (mode == FULL_TRACING)
	{
		if (curr_trace_buffer->isFull == 0 && readOff == writeOff)
		{
			curr_trace_buffer->isFull = 1;
		}
		for (int i = 0; i < num_of_arguements + 1; i++)
		{
			if (i == 0)
			{
				for (int j = 0; j < 8; j++)
				{
					buff[j + writeOff] = *((char *)(&syscall_num) + j);
				}
			}
			else if (i == 1)
			{
				for (int j = 0; j < 8; j++)
				{
					buff[j + writeOff] = *((char *)(&param1) + j);
				}
			}
			else if (i == 2)
			{
				for (int j = 0; j < 8; j++)
				{
					buff[j + writeOff] = *((char *)(&param2) + j);
				}
			}
			else if (i == 3)
			{
				for (int j = 0; j < 8; j++)
				{
					buff[j + writeOff] = *((char *)(&param3) + j);
				}
			}
			else if (i == 4)
			{
				for (int j = 0; j < 8; j++)
				{
					buff[j + writeOff] = *((char *)(&param4) + j);
				}
			}
			writeOff = (writeOff + 8) % 4096;
		}
	}
	else if (mode == FILTERED_TRACING)
	{
		int found = 0;
		struct strace_info *headpointer = current->st_md_base->next;
		while (headpointer != NULL)
		{
			if (headpointer->syscall_num == syscall_num)
			{
				found = 1;
				break;
			}
			headpointer = headpointer->next;
		}
		if (found)
		{
			if (curr_trace_buffer->isFull == 0 && readOff == writeOff)
			{
				curr_trace_buffer->isFull = 1;
			}
			for (int i = 0; i < num_of_arguements + 1; i++)
			{
				if (i == 0)
				{
					for (int j = 0; j < 8; j++)
					{
						buff[j + writeOff] = *((char *)(&syscall_num) + j);
					}
				}
				else if (i == 1)
				{
					for (int j = 0; j < 8; j++)
					{
						buff[j + writeOff] = *((char *)(&param1) + j);
					}
				}
				else if (i == 2)
				{
					for (int j = 0; j < 8; j++)
					{
						buff[j + writeOff] = *((char *)(&param2) + j);
					}
				}
				else if (i == 3)
				{
					for (int j = 0; j < 8; j++)
					{
						buff[j + writeOff] = *((char *)(&param3) + j);
					}
				}
				else if (i == 4)
				{
					for (int j = 0; j < 8; j++)
					{
						buff[j + writeOff] = *((char *)(&param4) + j);
					}
				}
				writeOff = (writeOff + 8) % 4096;
			}
		}
	}

	curr_trace_buffer->writeOffset = writeOff;
	return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	if (current == NULL)
	{
		return -EINVAL;
	}
	if (action != ADD_STRACE && action != REMOVE_STRACE)
	{
		return -EINVAL;
	}
	if (action == ADD_STRACE)
	{
		if (current->st_md_base == NULL)
		{
			struct strace_head *head = os_alloc(sizeof(struct strace_head));
			current->st_md_base = head;
			head->count = 0;
			head->is_traced = 0;
			head->last = NULL;
			head->next = NULL;
		}
		struct strace_info *head_info = current->st_md_base->next;
		struct strace_info *temp = NULL;
		if (head_info == NULL)
		{
			struct strace_info *new_strace = os_alloc(sizeof(struct strace_info));
			new_strace->syscall_num = syscall_num;
			new_strace->next = NULL;
			current->st_md_base->next = new_strace;
			current->st_md_base->last = new_strace;
			current->st_md_base->count++;
			return 0;
		}
		else
		{
			if (current->st_md_base->count == STRACE_MAX)
			{
				return -EINVAL;
			}
			while (head_info != NULL)
			{
				temp = head_info;
				if (head_info->syscall_num == syscall_num)
				{
					return -EINVAL;
				}
				else
				{
					head_info = head_info->next;
				}
			}

			struct strace_info *new_strace = os_alloc(sizeof(struct strace_info));
			new_strace->syscall_num = syscall_num;
			new_strace->next = NULL;
			temp->next = new_strace;
			current->st_md_base->last = new_strace;
			current->st_md_base->count++;
			return 0;
		}
	}
	else if (action == REMOVE_STRACE)
	{
		if (current->st_md_base == NULL)
		{
			return -EINVAL;
		}
		struct strace_info *head_info = current->st_md_base->next;
		if (head_info == NULL)
		{
			return -EINVAL;
		}
		if (head_info->syscall_num == syscall_num)
		{
			current->st_md_base->next = head_info->next;
			current->st_md_base->count--;
			os_free(head_info, sizeof(struct strace_info));
			return 0;
		}
		else
		{
			struct strace_info *temp = NULL;
			while (head_info != NULL && head_info->syscall_num != syscall_num)
			{
				temp = head_info;
				head_info = head_info->next;
			}
			if (head_info == NULL)
			{
				return -EINVAL;
			}
			temp->next = head_info->next;
			os_free(head_info, sizeof(struct strace_info));
			current->st_md_base->count--;
			return 0;
		}
	}
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	struct trace_buffer_info *curr_trace_buffer = filep->trace_buffer;
	if (curr_trace_buffer == NULL)
	{
		return -EINVAL;
	}
	char *trace_buff = (char *)curr_trace_buffer->buffer;
	if (trace_buff == NULL)
	{
		return -EINVAL;
	}
	if (filep->mode & O_READ == 0)
	{
		return -EINVAL;
	}
	int readOff = curr_trace_buffer->readOffset;
	int bytes_read = 0;
	if (readOff == curr_trace_buffer->writeOffset && curr_trace_buffer->isFull == 0)
	{
		return 0;
	}
	for (int i = 0; i < count; i++)
	{
		if (readOff == curr_trace_buffer->writeOffset)
		{
			curr_trace_buffer->readOffset = readOff;
			curr_trace_buffer->isFull = 0;
			return bytes_read;
		}
		u64 syscall_num = *((u64 *)(trace_buff + readOff));
		for (int j = 0; j < 8; j++)
		{
			buff[bytes_read + j] = trace_buff[readOff + j];
		}
		// printk("syscall num = %d\n", syscall_num);
		bytes_read += 8;
		readOff = (readOff + 8) % 4096;
		// printk("bytes read = %d, readOff = %d\n", bytes_read, readOff);

		int num_of_arguements = 0;
		if (syscall_num == 2 || syscall_num == 10 || syscall_num == 11 || syscall_num == 13 || syscall_num == 15 || syscall_num == 20 || syscall_num == 21 || syscall_num == 22 || syscall_num == 38)
		{
			// no arguements
			num_of_arguements = 0;
		}
		else if (syscall_num == 1 || syscall_num == 6 || syscall_num == 7 || syscall_num == 12 || syscall_num == 14 || syscall_num == 19 || syscall_num == 27 || syscall_num == 29 || syscall_num == 36)
		{
			// 1 arguement
			num_of_arguements = 1;
		}
		else if (syscall_num == 4 || syscall_num == 8 || syscall_num == 9 || syscall_num == 17 || syscall_num == 23 || syscall_num == 28 || syscall_num == 37 || syscall_num == 40)
		{
			// 2 arguements
			num_of_arguements = 2;
		}
		else if (syscall_num == 5 || syscall_num == 18 || syscall_num == 24 || syscall_num == 25 || syscall_num == 30 || syscall_num == 39 || syscall_num == 41)
		{
			// 3 arguements
			num_of_arguements = 3;
		}
		else if (syscall_num == 16 || syscall_num == 35)
		{
			// 4 arguements
			num_of_arguements = 4;
		}
		// printk("num of arguements = %d\n", num_of_arguements);

		for (int k = 0; k < num_of_arguements; k++)
		{
			for (int j = 0; j < 8; j++)
			{
				buff[bytes_read + j] = trace_buff[readOff + j];
			}
			bytes_read += 8;
			readOff = (readOff + 8) % 4096;
		}
	}
	curr_trace_buffer->readOffset = readOff;
	return bytes_read;
}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if (current == NULL)
	{
		return -EINVAL;
	}
	if (tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING)
	{
		return -EINVAL;
	}
	if (current->st_md_base == NULL)
	{
		struct strace_head *head = os_alloc(sizeof(struct strace_head));
		if (head == NULL)
		{
			return -EINVAL; // memalloc failure
		}
		current->st_md_base = head;
		head->count = 0;
		head->is_traced = 1;
		head->strace_fd = fd;
		head->tracing_mode = tracing_mode;
		head->next = NULL;
		head->last = NULL;
		return 0;
	}
	else
	{
		struct strace_head *head = current->st_md_base;
		head->is_traced = 1;
		head->strace_fd = fd;
		head->tracing_mode = tracing_mode;
		return 0;
	}
}

int sys_end_strace(struct exec_context *current)
{
	if (current == NULL)
	{
		return -EINVAL;
	}
	struct strace_head *head_list = current->st_md_base;
	struct strace_info *curr = head_list->next;
	struct strace_info *nex;
	while (curr != NULL)
	{
		nex = curr->next;
		os_free(curr, sizeof(struct strace_info));
		curr = nex;
	}
	os_free(head_list, sizeof(struct strace_head));
	current->st_md_base = NULL;
	return 0;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

long do_ftrace(struct exec_context *current, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	if (current == NULL)
	{
		return -EINVAL;
	}
	if (action == ADD_FTRACE)
	{
		if (current->ft_md_base == NULL)
		{
			current->ft_md_base = (struct ftrace_head *)os_alloc(sizeof(struct ftrace_head));
			if (current->ft_md_base == NULL)
			{
				return -EINVAL;
			}
			current->ft_md_base->count = 0;
			current->ft_md_base->last = NULL;
			current->ft_md_base->next = NULL;
		}
		if (current->ft_md_base->count == FTRACE_MAX)
		{
			return -EINVAL;
		}
		struct ftrace_info *head_info = current->ft_md_base->next, *temp = NULL;
		while (head_info != NULL && head_info->faddr != faddr)
		{
			temp = head_info;
			head_info = head_info->next;
		}
		if (head_info != NULL)
		{
			return -EINVAL; // function call found
		}
		if (nargs < 0)
		{
			return -EINVAL;
		}
		struct ftrace_info *new_info = (struct ftrace_info *)os_alloc(sizeof(struct ftrace_info));
		if (new_info == NULL)
		{
			return -EINVAL;
		}
		new_info->capture_backtrace = 0;
		new_info->faddr = faddr;
		new_info->fd = fd_trace_buffer;
		new_info->next = NULL;
		new_info->num_args = nargs;
		if (temp != NULL)
		{
			temp->next = new_info;
		}
		if (current->ft_md_base->next == NULL)
		{
			current->ft_md_base->next = new_info;
		}
		current->ft_md_base->last = new_info;
		current->ft_md_base->count++;
		return 0;
	}
	else if (action == REMOVE_FTRACE)
	{
		struct ftrace_head *head = current->ft_md_base;
		if (head == NULL || head->count == 0)
		{
			return -EINVAL;
		}
		struct ftrace_info *curr = head->next, *prev = NULL;
		while (curr != NULL && curr->faddr != faddr)
		{
			prev = curr;
			curr = curr->next;
		}
		if (curr == NULL)
		{
			return -EINVAL;
		}
		if (prev == NULL)
		{
			// first info to be removed
			if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
			{
				*((u8 *)faddr) = curr->code_backup[0]; // disabling the trace
				*((u8 *)faddr + 1) = curr->code_backup[1];
				*((u8 *)faddr + 2) = curr->code_backup[2];
				*((u8 *)faddr + 3) = curr->code_backup[3];
			}
			head->next = curr->next;
			head->count--;
			if (head->count == 0)
			{
				os_free(head, sizeof(struct ftrace_head));
			}
			os_free(curr, sizeof(struct ftrace_info));
		}
		else
		{
			// intermediate info to be removed
			if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
			{
				*((u8 *)faddr) = curr->code_backup[0]; // tracing enabled
				*((u8 *)faddr + 1) = curr->code_backup[1];
				*((u8 *)faddr + 2) = curr->code_backup[2];
				*((u8 *)faddr + 3) = curr->code_backup[3];
			}
			prev->next = curr->next;
			head->count--;
			os_free(curr, sizeof(struct ftrace_info));
		}
		return 0;
	}
	else if (action == ENABLE_FTRACE)
	{
		struct ftrace_head *head = current->ft_md_base;
		if (head == NULL)
		{
			return -EINVAL;
		}
		struct ftrace_info *curr = head->next;
		while (curr != NULL && curr->faddr != faddr)
		{
			curr = curr->next;
		}
		if (curr == NULL)
		{
			return -EINVAL; // function call not found in the ftrace list
		}
		// printk("faddr = %x \n", faddr);
		// printk("val = %x \n", *((u8 *)faddr));
		if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
		{
			return 0;
		}
		else
		{
			// printk("First enable\n");
			// printk("faddr = %x \n", *((u8 *)faddr));
			// printk("INV = %x \n", INV_OPCODE);
			curr->code_backup[0] = *((u8 *)faddr);
			curr->code_backup[1] = *((u8 *)faddr + 1);
			curr->code_backup[2] = *((u8 *)faddr + 2);
			curr->code_backup[3] = *((u8 *)faddr + 3);
			// printk("faddr = %x \n", curr->code_backup[0]);
			// printk("faddr = %x \n", curr->code_backup[1]);
			*((u8 *)faddr) = INV_OPCODE;
			*((u8 *)faddr + 1) = INV_OPCODE;
			*((u8 *)faddr + 2) = INV_OPCODE;
			*((u8 *)faddr + 3) = INV_OPCODE;
		}

		// printk("faddr = %x \n", faddr);
		// printk("val = %x \n", *((u8 *)faddr));
		// printk("%d, faddr = %lu \n", *(int*)faddr, faddr);
	}
	else if (action == DISABLE_FTRACE)
	{
		struct ftrace_head *head = current->ft_md_base;
		if (head == NULL)
		{
			return -EINVAL;
		}
		struct ftrace_info *curr = head->next;
		while (curr != NULL && curr->faddr != faddr)
		{
			curr = curr->next;
		}
		if (curr == NULL)
		{
			return -EINVAL; // function call not found in the ftrace list
		}
		// printk("Yes\n");
		// printk("%d\n", *((char *)faddr));
		// printk("%d\n", INV_OPCODE);

		if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
		{
			*((u8 *)faddr) = curr->code_backup[0]; // disabling the trace
			*((u8 *)faddr + 1) = curr->code_backup[1];
			*((u8 *)faddr + 2) = curr->code_backup[2];
			*((u8 *)faddr + 3) = curr->code_backup[3];
			// printk("%d\n", curr->code_backup[0]);
			// printk("%d\n", curr->code_backup[1]);
			// printk("%d\n", curr->code_backup[2]);
			// printk("%d\n", curr->code_backup[3]);
		}
		return 0;
	}
	else if (action == ENABLE_BACKTRACE)
	{
		struct ftrace_head *head = current->ft_md_base;
		if (head == NULL)
		{
			return -EINVAL;
		}
		struct ftrace_info *curr = head->next;
		while (curr != NULL && curr->faddr != faddr)
		{
			curr = curr->next;
		}
		if (curr == NULL)
		{
			return -EINVAL; // function call not found in the ftrace list
		}
		curr->capture_backtrace = 1;
		if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
		{
			return 0;
		}
		curr->code_backup[0] = *((u8 *)faddr);
		curr->code_backup[1] = *((u8 *)faddr + 1);
		curr->code_backup[2] = *((u8 *)faddr + 2);
		curr->code_backup[3] = *((u8 *)faddr + 3);
		*((u8 *)faddr) = INV_OPCODE;
		*((u8 *)faddr + 1) = INV_OPCODE;
		*((u8 *)faddr + 2) = INV_OPCODE;
		*((u8 *)faddr + 3) = INV_OPCODE;
	}
	else if (action == DISABLE_BACKTRACE)
	{
		struct ftrace_head *head = current->ft_md_base;
		if (head == NULL)
		{
			return -EINVAL;
		}
		struct ftrace_info *curr = head->next;
		while (curr != NULL && curr->faddr != faddr)
		{
			curr = curr->next;
		}
		if (curr == NULL)
		{
			return -EINVAL; // function call not found in the ftrace list
		}
		curr->capture_backtrace = 0;
		if (*((u8 *)faddr) == INV_OPCODE && *((u8 *)faddr + 1) == INV_OPCODE && *((u8 *)faddr + 2) == INV_OPCODE && *((u8 *)faddr + 3) == INV_OPCODE)
		{
			*((u8 *)faddr) = curr->code_backup[0]; // disabling the trace
			*((u8 *)faddr + 1) = curr->code_backup[1];
			*((u8 *)faddr + 2) = curr->code_backup[2];
			*((u8 *)faddr + 3) = curr->code_backup[3];
		}
		return 0;
	}
	else
	{
		return -EINVAL;
	}
	return 0;
}

long handle_ftrace_fault(struct user_regs *regs)
{
	struct exec_context *current = get_current_ctx();
	u64 faddr = regs->entry_rip;
	struct ftrace_info *func = current->ft_md_base->next;
	long bytes_read = 0;
	// printk("Fault raised!!\n");

	if (current == NULL)
	{
		return -EINVAL;
	}

	while (func != NULL && func->faddr != faddr)
	{
		func = func->next;
	}
	if (func == NULL)
	{
		return -EINVAL;
	}

	int nargs = func->num_args;
	int fd = func->fd;
	struct file *filep = current->files[fd];
	u64 *trace_buff = (u64 *)filep->trace_buffer->buffer;
	int is_back_trace_enabled = func->capture_backtrace;

	// printk("fd = %d, nargs = %d\n", fd, nargs);

	regs->entry_rip = regs->entry_rip + 4;
	regs->entry_rsp = regs->entry_rsp - 8;
	*(u64 *)(regs->entry_rsp) = regs->rbp;
	regs->rbp = regs->entry_rsp;

	if ((filep->mode & O_WRITE) == 0)
	{
		return -EINVAL; // no write permission in trace buffer
	}
	if (filep->trace_buffer->readOffset == filep->trace_buffer->writeOffset && filep->trace_buffer->isFull == 1)
	{
		return -EINVAL;
	}

	filep->trace_buffer->writeOffset = (filep->trace_buffer->writeOffset + 8) % 4096; // space for storing delimiter

	if (is_back_trace_enabled)
	{
		// normal call tracing
		for (int i = 0; i < nargs + 1; i++)
		{
			if (i == 0)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = faddr;
			}
			else if (i == 1)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rdi;
			}
			else if (i == 2)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rsi;
			}
			else if (i == 3)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rdx;
			}
			else if (i == 4)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rcx;
			}
			else if (i == 5)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->r8;
			}
			else if (i == 6)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->r9;
			}
			bytes_read += 8;
			filep->trace_buffer->writeOffset = (filep->trace_buffer->writeOffset + 8) % 4096;
		}
		*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = faddr;
		filep->trace_buffer->writeOffset = (filep->trace_buffer->writeOffset + 8) % 4096;
		filep->offp = filep->trace_buffer->writeOffset;
		bytes_read += 8;

		// backtracing starts
		u64 rbp = regs->rbp;

		while (*((u64 *)rbp + 1) != END_ADDR)
		{
			*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = *((u64 *)rbp + 1);
			filep->trace_buffer->writeOffset = (filep->trace_buffer->writeOffset + 8) % 4096;
			filep->offp = filep->trace_buffer->writeOffset;
			bytes_read += 8;
			rbp = *(u64 *)rbp;
		}

		// backtracing ends
	}
	else
	{
		// printk("Here\n");
		for (int i = 0; i < nargs + 1; i++)
		{
			if (i == 0)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = faddr;
				// printk("faddr = %x\n", faddr);
			}
			else if (i == 1)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rdi;
				// printk("1st arg = %d\n", regs->rdi);
			}
			else if (i == 2)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rsi;
				// printk("2nd arg = %d\n", regs->rsi);
			}
			else if (i == 3)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rdx;
			}
			else if (i == 4)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->rcx;
			}
			else if (i == 5)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->r8;
			}
			else if (i == 6)
			{
				*(u64 *)((u8 *)trace_buff + filep->trace_buffer->writeOffset) = regs->r9;
			}
			bytes_read += 8;
			filep->trace_buffer->writeOffset = (filep->trace_buffer->writeOffset + 8) % 4096;
		}
		filep->offp = filep->trace_buffer->writeOffset;
	}

	/// add code for adding delimiter
	///
	///
	///
	*(u64 *)((u8 *)trace_buff + (filep->trace_buffer->writeOffset - bytes_read - 8 + 4096) % 4096) = bytes_read;
	filep->offp = (filep->trace_buffer->writeOffset - bytes_read - 8 + 4096) % 4096;
	///
	///
	///
	///
	///
	if (filep->trace_buffer->readOffset == filep->trace_buffer->writeOffset && filep->trace_buffer->isFull == 0)
	{
		filep->trace_buffer->isFull = 1;
	}
	// printk("bytes wrote = %d\n", filep->trace_buffer->writeOffset);

	return 0;
}

int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	// check whether filep has read permissions
	if ((filep->mode & O_READ) == 0)
	{
		return -EINVAL;
	}

	u64 *trace_buff = (u64 *)filep->trace_buffer->buffer;

	if (filep->trace_buffer->readOffset == filep->trace_buffer->writeOffset && filep->trace_buffer->isFull == 0)
	{
		return 0;
	}

	long bytes_read = 0;
	u64 length;
	// printk("Yes in read\n");

	for (int i = 0; i < count; i++)
	{
		// printk("%d\n", i);
		length = *(u64 *)((u8 *)trace_buff + filep->trace_buffer->readOffset);
		// printk("length = %d\n", length);

		filep->trace_buffer->readOffset = (filep->trace_buffer->readOffset + 8) % 4096;
		for (int i = 0; i < length; i++)
		{
			*(buff + bytes_read) = *((u8 *)trace_buff + filep->trace_buffer->readOffset);
			filep->trace_buffer->readOffset = (filep->trace_buffer->readOffset + 1) % 4096;
			bytes_read += 1;
		}
		filep->offp = filep->trace_buffer->readOffset;
		if (filep->trace_buffer->readOffset == filep->trace_buffer->writeOffset)
		{
			filep->trace_buffer->isFull = 0;

			break;
		}
		// printk("read offset = %d\n", filep->trace_buffer->readOffset);
	}
	// printk("bytes read = %d\n", bytes_read);

	return bytes_read;
}