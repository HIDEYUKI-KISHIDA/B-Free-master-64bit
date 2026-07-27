#ifndef BFREE_ELF_USER_EXEC_H
#define BFREE_ELF_USER_EXEC_H

#include <stddef.h>

struct bfree_fs;
struct bfree_vnode;

/* Multi-MB ET_EXEC headroom (BusyBox ~1.2MiB; leave room for growth). */
#define BFREE_USER_EXEC_BLOB_MAX 0x800000UL /* 8 MiB */

int bfree_user_execve_ring3(struct bfree_fs *fs, const char *path,
			    char *const argv[], char *const envp[]);
int bfree_user_exec_pending(void);
void bfree_user_exec_clear(void);
void bfree_syscall_exec_resume_if_needed(void);

size_t bfree_user_exec_blob_max(void);
int bfree_user_exec_resolve(struct bfree_fs *fs, const char *path,
			    char *out_path, size_t out_len,
			    struct bfree_vnode **file_out);

#endif
