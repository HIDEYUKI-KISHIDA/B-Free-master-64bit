#ifndef BFREE_FREESTANDING_STRING_H
#define BFREE_FREESTANDING_STRING_H

#include <stddef.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int strcmp(const char *a, const char *b);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
size_t strlen(const char *s);

#endif
