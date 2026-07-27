/*
 * Device nodes /dev/null and /dev/zero (M5).
 */
#ifndef BFREE_DEVNODE_H
#define BFREE_DEVNODE_H

#include "fs_ofd.h"

#include <stddef.h>
#include <sys/types.h>

void bfree_devnodes_init(struct bfree_fs *fs);
ssize_t bfree_dev_read(struct bfree_fs *fs, int fd, void *buf, size_t count);
ssize_t bfree_dev_write(struct bfree_fs *fs, int fd, const void *buf,
			size_t count);

#endif /* BFREE_DEVNODE_H */
