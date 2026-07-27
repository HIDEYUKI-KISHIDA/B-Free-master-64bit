#include "debugcon.h"

#include <stddef.h>
#include <stdio.h>

void bfree_debug_write(const char *buf, size_t len)
{
	if (buf != NULL && len > 0)
		fwrite(buf, 1, len, stdout);
}

void bfree_debug_puts(const char *s)
{
	if (s != NULL)
		fputs(s, stdout);
}
