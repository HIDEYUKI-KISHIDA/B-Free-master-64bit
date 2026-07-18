#ifndef BFREE_CACHE_H
#define BFREE_CACHE_H

#include <stddef.h>

void cache_flush_range(void *addr, size_t size);
void cache_flush_all(void);

#endif /* BFREE_CACHE_H */
