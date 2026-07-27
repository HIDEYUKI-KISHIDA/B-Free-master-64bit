#ifndef BFREE_FREESTANDING_STDLIB_H
#define BFREE_FREESTANDING_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#endif
