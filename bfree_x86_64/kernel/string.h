#ifndef BFREE_STRING_H
#define BFREE_STRING_H

#include <stddef.h>


size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);

#endif
