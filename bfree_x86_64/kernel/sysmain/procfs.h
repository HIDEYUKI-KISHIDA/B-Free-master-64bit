#ifndef BFREE_PROCFS_H
#define BFREE_PROCFS_H

#include <stdint.h>

struct bfree_fs;

/* Create /proc /proc/self /proc/self/exe /proc/self/maps. */
int bfree_procfs_init(struct bfree_fs *fs);

/*
 * Update /proc/self/exe target and a minimal maps dump after an exec.
 * exe_path must be an absolute VFS path to the ET_EXEC file.
 */
int bfree_procfs_on_exec(struct bfree_fs *fs, const char *exe_path,
			 uintptr_t heap_base, uintptr_t stack_top);

#endif
