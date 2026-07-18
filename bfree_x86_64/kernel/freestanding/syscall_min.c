#include "debugcon.h"

#include <stddef.h>

void guest_init(void)
{
}

long bfree_invoke_syscall(unsigned long nr, unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3, unsigned long a4,
			  unsigned long a5)
{
	(void)a3;
	(void)a4;
	(void)a5;

	switch (nr) {
	case 1:
		bfree_debug_write((const char *)a1, (size_t)a2);
		return (long)a2;
	case 60:
		for (;;)
			__asm__ volatile("hlt");
	default:
		return -38; /* ENOSYS */
	}
}
