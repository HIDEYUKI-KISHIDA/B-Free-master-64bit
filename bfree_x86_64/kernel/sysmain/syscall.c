/*
 * Linux syscall dispatch for bfree_x86_64 guest (M1–M4).
 */
#include "elf_load.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "process.h"
#include "thread.h"
#include "tty.h"

#include <stddef.h>
#include <stdint.h>

struct guest_state {
	struct bfree_fs       fs;
	struct bfree_proc_mgr proc;
	struct guest_io       io;
};

static struct guest_state guest;

void guest_init(void)
{
	bfree_fs_init(&guest.fs);
	bfree_proc_init(&guest.proc);
	bfree_proc_attach_fs(&guest.fs);
	guest_io_init(&guest.io, &guest.fs, &guest.proc);
}

struct bfree_proc_mgr *guest_proc_mgr(void)
{
	return &guest.proc;
}

struct bfree_fs *guest_fs(void)
{
	return &guest.fs;
}

struct guest_io *guest_io_ctx(void)
{
	return &guest.io;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
	return guest_read(&guest.io, fd, buf, count);
}

ssize_t sys_write(int fd, const void *buf, size_t count)
{
	return guest_write(&guest.io, fd, buf, count);
}

off_t sys_lseek(int fd, off_t offset, int whence)
{
	return guest_lseek(&guest.io, fd, offset, whence);
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
	return guest_close(&guest.io, fd);
}

int sys_dup(int fd)
{
	return guest_dup(&guest.io, fd);
}

int sys_dup2(int oldfd, int newfd)
{
	return guest_dup2(&guest.io, oldfd, newfd);
}

int sys_fcntl(int fd, int cmd, long arg)
{
	return guest_fcntl(&guest.io, fd, cmd, arg);
}

int sys_chdir(const char *path)
{
	return bfree_chdir(&guest.fs, path);
}

int sys_getcwd(char *buf, size_t size)
{
	return bfree_getcwd(&guest.fs, buf, size);
}

uintptr_t sys_brk(uintptr_t addr)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return (uintptr_t)-1;
	return bfree_brk(&self->as, addr);
}

void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	(void)addr;
	(void)fd;
	(void)off;
	(void)prot;
	if (self == NULL)
		return (void *)(intptr_t)-ESRCH;
	return bfree_mmap(&self->as, NULL, len, prot, flags);
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

int sys_clone(unsigned long flags, void *stack, int *parent_tid, void *tls,
	      int *child_tid)
{
	return bfree_clone(&guest.proc, flags, stack, parent_tid, tls, child_tid,
			   NULL, NULL);
}

int sys_futex(int *uaddr, int op, int val, const void *timeout)
{
	return bfree_futex(uaddr, op, val, timeout);
}

int sys_setsid(void)
{
	return bfree_setsid(&guest.proc);
}

int sys_setpgid(int pid, int pgid)
{
	return bfree_setpgid(&guest.proc, pid, pgid);
}

int sys_getpgid(int pid)
{
	return bfree_getpgid(&guest.proc, pid);
}

int sys_ioctl(int fd, unsigned long req, void *arg)
{
	(void)fd;
	(void)arg;
	if (req == 0x5402) /* TIOCSPGRP */
		return bfree_tcsetpgrp(0, *(int *)arg);
	if (req == 0x540f) /* TIOCGPGRP */
		return bfree_tcgetpgrp(0);
	return -EINVAL;
}
