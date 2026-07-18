/*
 * Linux syscall dispatch for bfree_x86_64 guest (M1 FS + M2 process).
 */
#include "fs_ofd.h"
#include "process.h"

#include <stddef.h>
#include <stdint.h>

struct guest_state {
	struct bfree_fs fs;
	struct bfree_proc_mgr proc;
};

static struct guest_state guest;

void guest_init(void)
{
	bfree_fs_init(&guest.fs);
	bfree_proc_init(&guest.proc);
}

struct bfree_proc_mgr *guest_proc_mgr(void)
{
	return &guest.proc;
}

int sys_getdents64(int fd, void *buf, size_t count)
{
	return (int)bfree_getdents64(&guest.fs, fd, buf, count);
}

int sys_open(const char *path, int flags, int mode)
{
	return bfree_open(&guest.fs, path, flags, mode);
}

int sys_openat(int dirfd, const char *path, int flags, int mode)
{
	return bfree_openat(&guest.fs, dirfd, path, flags, mode);
}

int sys_unlink(const char *path)
{
	return bfree_unlink(&guest.fs, path);
}

int sys_unlinkat(int dirfd, const char *path, int flags)
{
	return bfree_unlinkat(&guest.fs, dirfd, path, flags);
}

int sys_mkdirat(int dirfd, const char *path, int mode)
{
	return bfree_mkdirat(&guest.fs, dirfd, path, mode);
}

int sys_close(int fd)
{
	return bfree_close(&guest.fs, fd);
}

int sys_dup(int fd)
{
	return bfree_dup(&guest.fs, fd);
}

int sys_vfork(void)
{
	return bfree_vfork(&guest.proc);
}

int sys_execve(const char *path, char *const argv[], char *const envp[])
{
	return bfree_execve(&guest.proc, path, (char **)argv, (char **)envp);
}

void sys_exit(int status)
{
	bfree_exit(&guest.proc, status);
}

int sys_wait4(int pid, int *status, int options, void *rusage)
{
	return bfree_wait4(&guest.proc, pid, status, options, rusage);
}

int sys_waitid(int idtype, int id, void *siginfo, int options)
{
	int status = 0;
	int rc;

	(void)siginfo;
	rc = bfree_waitid(&guest.proc, idtype, id, &status, options);
	if (rc >= 0 && siginfo != NULL)
		*(int *)siginfo = status;
	return rc;
}

int sys_fork(void)
{
	return bfree_fork(&guest.proc);
}

int sys_pipe(int pipefd[2])
{
	return bfree_pipe_open(&guest.proc, pipefd);
}

int sys_kill(int pid, int sig)
{
	bfree_kill(&guest.proc, pid, sig);
	return 0;
}
