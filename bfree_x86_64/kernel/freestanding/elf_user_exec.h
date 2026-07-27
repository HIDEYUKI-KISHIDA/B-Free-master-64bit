#ifndef BFREE_ELF_USER_EXEC_H
#define BFREE_ELF_USER_EXEC_H

struct bfree_fs;

int bfree_user_execve_ring3(struct bfree_fs *fs, const char *path,
			    char *const argv[], char *const envp[]);
int bfree_user_exec_pending(void);
void bfree_user_exec_clear(void);
void bfree_syscall_exec_resume_if_needed(void);

#endif
