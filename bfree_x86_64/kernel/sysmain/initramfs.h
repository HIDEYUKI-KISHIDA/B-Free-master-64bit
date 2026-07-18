#ifndef BFREE_INITRAMFS_H
#define BFREE_INITRAMFS_H

#include <stddef.h>
#include <stdint.h>

int bfree_initramfs_parse(const void *base, size_t len);
int bfree_initramfs_lookup(const char *name, const void **data, size_t *size);
int bfree_initramfs_file_count(void);

#endif
