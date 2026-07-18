#include "string.h"

size_t strlen(const char *s) {
    size_t n = 0;
    while (s && *s++) ++n;
    return n;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) {
        return 0;
    }
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *dst_bytes = (unsigned char *)dest;
    const unsigned char *src_bytes = (const unsigned char *)src;

    for (size_t index = 0; index < n; ++index) {
        dst_bytes[index] = src_bytes[index];
    }

    return dest;
}

void *memset(void *dest, int value, size_t n) {
    unsigned char *dst_bytes = (unsigned char *)dest;

    for (size_t index = 0; index < n; ++index) {
        dst_bytes[index] = (unsigned char)value;
    }

    return dest;
}
