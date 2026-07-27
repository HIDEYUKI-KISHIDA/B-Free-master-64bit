#ifndef BFREE_DEBUGCON_H
#define BFREE_DEBUGCON_H

#include <stddef.h>

void bfree_debug_write(const char *buf, size_t len);
void bfree_debug_puts(const char *s);

#endif
