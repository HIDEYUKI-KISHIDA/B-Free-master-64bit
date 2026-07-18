/*
 * Linux syscall dispatch for bfree_x86_64 guest (M1–M5).
 */
#include "cred.h"
#include "elf_load.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "ipc_shm.h"
#include "mount.h"
#include "net_unix.h"
#include "process.h"
#include "syscall_dispatch.h"
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
	bfree_syscall_registry_init();
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

int sys_mount(const char *target, const char *source, const char *fstype,
	      unsigned long flags, const void *data)
{
	(void)fstype;
	(void)flags;
	(void)data;
	return bfree_mount(&guest.fs, target, source);
}

int sys_umount2(const char *target, int flags)
{
	return bfree_umount2(&guest.fs, target, flags);
}

unsigned int sys_getuid(void)
{
	return bfree_getuid();
}

unsigned int sys_geteuid(void)
{
	return bfree_geteuid();
}

unsigned int sys_getgid(void)
{
	return bfree_getgid();
}

unsigned int sys_getegid(void)
{
	return bfree_getegid();
}

int sys_setuid(unsigned int uid)
{
	return bfree_setuid(uid);
}

int sys_setgid(unsigned int gid)
{
	return bfree_setgid(gid);
}

int sys_setreuid(unsigned int ruid, unsigned int euid)
{
	return bfree_setreuid(ruid, euid);
}

int sys_setregid(unsigned int rgid, unsigned int egid)
{
	return bfree_setregid(rgid, egid);
}

int sys_socket(int domain, int type, int protocol)
{
	return bfree_socket(domain, type, protocol);
}

int sys_bind(int sockfd, const struct bfree_sockaddr_un *addr,
	     unsigned int addrlen)
{
	return bfree_bind(sockfd, addr, addrlen);
}

int sys_connect(int sockfd, const struct bfree_sockaddr_un *addr,
		unsigned int addrlen)
{
	return bfree_connect(sockfd, addr, addrlen);
}

ssize_t sys_sendto(int sockfd, const void *buf, unsigned int len, int flags,
		   const struct bfree_sockaddr_un *addr, unsigned int addrlen)
{
	return bfree_sendto(sockfd, buf, len, flags, addr, addrlen);
}

ssize_t sys_recvfrom(int sockfd, void *buf, unsigned int len, int flags,
		     struct bfree_sockaddr_un *addr, unsigned int *addrlen)
{
	return bfree_recvfrom(sockfd, buf, len, flags, addr, addrlen);
}

int sys_socketpair(int domain, int type, int protocol, int sv[2])
{
	return bfree_socketpair(domain, type, protocol, sv);
}

int sys_shmget(int key, unsigned long size, int shmflg)
{
	return bfree_shmget(key, size, shmflg);
}

void *sys_shmat(int shmid, const void *shmaddr, int shmflg)
{
	return bfree_shmat(shmid, shmaddr, shmflg);
}

int sys_shmdt(const void *shmaddr)
{
	return bfree_shmdt(shmaddr);
}

int sys_shmctl(int shmid, int cmd, struct bfree_shmid_ds *buf)
{
	return bfree_shmctl(shmid, cmd, buf);
}
