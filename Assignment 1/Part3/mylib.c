#include <stdio.h>

void *memalloc(unsigned long size) 
{
	printf("memalloc() called\n");
	return NULL;
}

int memfree(void *ptr)
{
	printf("memfree() called\n");
	return 0;
}	
