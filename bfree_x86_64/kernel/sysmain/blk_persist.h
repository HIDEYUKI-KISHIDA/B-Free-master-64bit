/*
 * blk_persist.h — block-backed file I/O helpers (mount API in fs_ofd.h).
 */
#ifndef BFREE_BLK_PERSIST_H
#define BFREE_BLK_PERSIST_H

#include "fs_ofd.h"

ssize_t bfree_blk_file_read(struct bfree_fs *fs, struct bfree_vnode *vn,
			    off_t offset, void *buf, size_t count);
ssize_t bfree_blk_file_write(struct bfree_fs *fs, struct bfree_vnode *vn,
			     off_t offset, const void *buf, size_t count);
void    bfree_blk_file_free(struct bfree_fs *fs, struct bfree_vnode *vn);

#endif /* BFREE_BLK_PERSIST_H */
