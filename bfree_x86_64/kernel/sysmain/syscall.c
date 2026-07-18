/*
 * Linux syscall dispatch stubs for bfree_x86_64 guest.
 *
 * Full guest integration wires these to the cooperative scheduler in
 * process.c and the page tables in vmm.c.  Filesystem syscalls delegate to
 * fs_ofd.c where directory cursors are per-OFD (M1).
 */
#include "fs_ofd.h"

#include <stddef.h>
#include <stdint.h>

struct guest_fs_state {
	struct bfree_fs fs;
};

static struct guest_fs_state guest;

void guest_fs_init(void)
{
	bfree_fs_init(&guest.fs);
}

int sys_getdents64(int fd, void *buf, size_t count)
{
	return (int)bfree_getdents64(&guest.fs, fd, buf, count);
}

int sys_open(const char *path, int flags, int mode)
{
	return bfree_open(&guest.fs, path, flags, mode);
}

int sys_close(int fd)
{
	return bfree_close(&guest.fs, fd);
}

int sys_dup(int fd)
{
	return bfree_dup(&guest.fs, fd);
}

/* process.c / vmm.c: vfork, execve, ENOSYS fork — see POSIX roadmap M2. */
int sys_fork(void)
{
	return -38; /* ENOSYS */
}

int sys_vfork(void);
int sys_execve(const char *path, char *const argv[], char *const envp[]);
int sys_wait4(int pid, int *status, int options, void *rusage);
