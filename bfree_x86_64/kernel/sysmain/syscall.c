/*
 * Linux syscall dispatch for bfree_x86_64 guest (M1–M6).
 */
#include "cred.h"
#include "elf_load.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "ipc_shm.h"
#include "mount.h"
#include "net_unix.h"
#include "process.h"
#include "process.h"
#include "syscall_dispatch.h"
#include "thread.h"
#include "tty.h"

#include <errno.h>
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

int sys_getpid(void)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return 1;
	return self->pid;
}

int sys_getppid(void)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return 0;
	return self->ppid;
}

int sys_rt_sigaction(int sig, const void *act, void *oact, size_t sigsetsize)
{
	(void)sig;
	(void)act;
	(void)oact;
	(void)sigsetsize;
	return 0;
}

int sys_rt_sigprocmask(int how, const void *set, void *oset, size_t sigsetsize)
{
	(void)how;
	(void)set;
	(void)oset;
	(void)sigsetsize;
	return 0;
}

void sys_rt_sigreturn(void)
{
}

int sys_capget(void *hdrp, void *datap)
{
	(void)hdrp;
	(void)datap;
	return 0;
}

int sys_capset(void *hdrp, const void *datap)
{
	(void)hdrp;
	(void)datap;
	return 0;
}

int sys_prlimit64(int pid, unsigned int resource, const void *new_rlim,
		  void *old_rlim)
{
	(void)pid;
	(void)resource;
	(void)new_rlim;
	(void)old_rlim;
	return 0;
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

int sys_nanosleep(const void *req, void *rem)
{
	(void)req;
	(void)rem;
	return 0;
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

int sys_stat(const char *path, struct bfree_linux_stat *st)
{
	return bfree_stat(&guest.fs, path, st);
}

int sys_fstat(int fd, struct bfree_linux_stat *st)
{
	return bfree_fstat(&guest.fs, fd, st);
}

int sys_newfstatat(int dirfd, const char *path, struct bfree_linux_stat *st,
		   int flags)
{
	return bfree_fstatat(&guest.fs, dirfd, path, st, flags);
}

int sys_poll(struct bfree_pollfd *fds, unsigned int nfds, int timeout)
{
	return bfree_poll(&guest.proc, &guest.fs, fds, nfds, timeout);
}

static long sysret_long(long rc)
{
	if (rc < 0 && rc > -4096)
		return rc;
	return rc;
}

long bfree_invoke_syscall(unsigned long nr, unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3, unsigned long a4,
			  unsigned long a5)
{
	if (!bfree_syscall_is_implemented(nr))
		return -ENOSYS;

	switch (nr) {
	case 0:
		return sysret_long(sys_read((int)a0, (void *)a1, (size_t)a2));
	case 1:
		return sysret_long(sys_write((int)a0, (const void *)a1,
					     (size_t)a2));
	case 2:
		return sys_open((const char *)a0, (int)a1, (int)a2);
	case 3:
		return sys_close((int)a0);
	case 4:
		return sys_stat((const char *)a0,
				(struct bfree_linux_stat *)a1);
	case 5:
		return sys_fstat((int)a0, (struct bfree_linux_stat *)a1);
	case 7:
		return sys_poll((struct bfree_pollfd *)a0, (unsigned int)a1,
				(int)a2);
	case 13:
		return sys_rt_sigaction((int)a0, (const void *)a1, (void *)a2,
					(size_t)a3);
	case 14:
		return sys_rt_sigprocmask((int)a0, (const void *)a1, (void *)a2,
					  (size_t)a3);
	case 15:
		sys_rt_sigreturn();
		return 0;
	case 16:
		return sys_ioctl((int)a0, a1, (void *)a2);
	case 32:
		return sys_dup((int)a0);
	case 33:
		return sys_dup2((int)a0, (int)a1);
	case 35:
		return sys_nanosleep((const void *)a0, (void *)a1);
	case 39:
		return sys_getpid();
	case 87:
		return sys_unlink((const char *)a0);
	case 90:
		return sys_capget((void *)a0, (void *)a1);
	case 91:
		return sys_capset((void *)a0, (const void *)a1);
	case 110:
		return sys_getppid();
	case 121:
		return sys_getpgid((int)a0);
	case 157:
		return sys_prlimit64((int)a0, (unsigned int)a1,
				     (const void *)a2, (void *)a3);
	case 217:
		return sys_getdents64((int)a0, (void *)a1, (size_t)a2);
	case 257:
		return sys_openat((int)a0, (const char *)a1, (int)a2, (int)a3);
	case 258:
		return sys_mkdirat((int)a0, (const char *)a1, (int)a2);
	case 262:
		return sys_newfstatat((int)a0, (const char *)a1,
				      (struct bfree_linux_stat *)a2, (int)a3);
	case 263:
		return sys_unlinkat((int)a0, (const char *)a1, (int)a2);
	case 8:
		return sysret_long(sys_lseek((int)a0, (off_t)a1, (int)a2));
	case 9:
		return (long)(uintptr_t)sys_mmap((void *)a0, (size_t)a1,
						 (int)a2, (int)a3, (int)a4,
						 (off_t)a5);
	case 12:
		return (long)sys_brk((uintptr_t)a0);
	case 22:
		return sys_pipe((int *)a0);
	case 29:
		return sys_shmget((int)a0, (unsigned long)a1, (int)a2);
	case 30:
		return (long)(uintptr_t)sys_shmat((int)a0, (const void *)a1,
						   (int)a2);
	case 31:
		return sys_shmctl((int)a0, (int)a1,
				  (struct bfree_shmid_ds *)a2);
	case 41:
		return sys_socket((int)a0, (int)a1, (int)a2);
	case 42:
		return sys_connect((int)a0,
				   (const struct bfree_sockaddr_un *)a1,
				   (unsigned int)a2);
	case 44:
		return sysret_long(sys_sendto((int)a0, (const void *)a1,
					      (unsigned int)a2, (int)a3,
					      (const struct bfree_sockaddr_un *)a4,
					      (unsigned int)a5));
	case 45:
		return sysret_long(sys_recvfrom((int)a0, (void *)a1,
					       (unsigned int)a2, (int)a3,
					       (struct bfree_sockaddr_un *)a4,
					       (unsigned int *)a5));
	case 49:
		return sys_bind((int)a0,
				(const struct bfree_sockaddr_un *)a1,
				(unsigned int)a2);
	case 53:
		return sys_socketpair((int)a0, (int)a1, (int)a2, (int *)a3);
	case 56:
		return sys_clone(a0, (void *)a1, (int *)a2, (void *)a3,
				 (int *)a4);
	case 57:
		return sys_fork();
	case 58:
		return sys_vfork();
	case 59:
		return sys_execve((const char *)a0, (char *const *)a1,
				  (char *const *)a2);
	case 60:
		sys_exit((int)a0);
		return 0;
	case 61:
		return sys_wait4((int)a0, (int *)a1, (int)a2, (void *)a3);
	case 62:
		return sys_kill((int)a0, (int)a1);
	case 67:
		return sys_shmdt((const void *)a0);
	case 72:
		return sys_fcntl((int)a0, (int)a1, (long)a2);
	case 79:
		return sys_getcwd((char *)a0, (size_t)a1);
	case 80:
		return sys_chdir((const char *)a0);
	case 102:
		return (long)sys_getuid();
	case 104:
		return (long)sys_getgid();
	case 105:
		return sys_setuid((unsigned int)a0);
	case 106:
		return sys_setgid((unsigned int)a0);
	case 107:
		return (long)sys_geteuid();
	case 108:
		return (long)sys_getegid();
	case 109:
		return sys_setpgid((int)a0, (int)a1);
	case 112:
		return sys_setsid();
	case 113:
		return sys_setreuid((unsigned int)a0, (unsigned int)a1);
	case 114:
		return sys_setregid((unsigned int)a0, (unsigned int)a1);
	case 165:
		return sys_mount((const char *)a0, (const char *)a1,
				 (const char *)a2, a3, (const void *)a4);
	case 166:
		return sys_umount2((const char *)a0, (int)a1);
	case 202:
		return sys_futex((int *)a0, (int)a1, (int)a2,
				 (const void *)a3);
	case 247:
		return sys_waitid((int)a0, (int)a1, (void *)a2, (int)a3);
	default:
		return -ENOSYS;
	}
}
