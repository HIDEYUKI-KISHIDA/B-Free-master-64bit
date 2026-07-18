#ifndef BFREE_MOUNT_H
#define BFREE_MOUNT_H

struct bfree_fs;

int bfree_mount(struct bfree_fs *fs, const char *target, const char *source);
int bfree_umount2(struct bfree_fs *fs, const char *target, int flags);

#endif
