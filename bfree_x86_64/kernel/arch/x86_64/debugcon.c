#include "debugcon.h"

#define DEBUGCON_PORT 0xe9

void bfree_debug_write(const char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		__asm__ volatile("outb %%al, %%dx" : : "a"(buf[i]), "d"(DEBUGCON_PORT));
}

void bfree_debug_puts(const char *s)
{
	while (*s != '\0')
		__asm__ volatile("outb %%al, %%dx" : : "a"(*s++), "d"(DEBUGCON_PORT));
}
