/*
 * Linux syscall dispatch for bfree_x86_64 guest (M17 ABI holes 0–4).
 */
#include "cred.h"
#include "elf_load.h"
#include "epoll.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "ipc_shm.h"
#include "ipc_sysv.h"
#include "mount.h"
#include "net_unix.h"
#include "process.h"
#include "sched_abi.h"
#include "signal_frame.h"
#include "syscall_dispatch.h"
#include "thread.h"
#include "tty.h"
#include "vmm.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef O_CREAT
#define O_CREAT 0100
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_TRUNC
#define O_TRUNC 01000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#define BFREE_RLIMIT_NLIMITS 16
#define BFREE_STUB_FD_BASE   300
#define BFREE_STUB_FD_MAX    32
#define BFREE_EVENTFD_MAX    16

struct guest_timespec {
	long tv_sec;
	long tv_nsec;
};

struct guest_timeval {
	long tv_sec;
	long tv_usec;
};

struct guest_iovec {
	void *iov_base;
	size_t iov_len;
};

struct guest_rlimit64 {
	uint64_t rlim_cur;
	uint64_t rlim_max;
};

struct guest_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

struct guest_statfs {
	long f_type;
	long f_bsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	uint64_t f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

struct guest_state {
	struct bfree_fs       fs;
	struct bfree_proc_mgr proc;
	struct guest_io       io;
};

static struct guest_state guest;
static struct guest_rlimit64 g_rlimits[BFREE_RLIMIT_NLIMITS];
static int g_rlimits_init;
static unsigned long g_arch_fsbase;
static unsigned g_alarm_sec;
static struct guest_timeval g_itimer_value;
static struct guest_timeval g_itimer_interval;
static int g_stub_fds[BFREE_STUB_FD_MAX];
static int g_stub_n;
static uint64_t g_eventfd_cnt[BFREE_EVENTFD_MAX];
static int g_eventfd_used[BFREE_EVENTFD_MAX];
static int g_memfd_seq;

void guest_init(void)
{
	unsigned i;

	bfree_syscall_registry_init();
	bfree_fs_init(&guest.fs);
	bfree_proc_init(&guest.proc);
	bfree_proc_attach_fs(&guest.fs);
	bfree_proc_bind_fs(&guest.proc, &guest.fs);
	guest_io_init(&guest.io, &guest.fs, &guest.proc);
	bfree_epoll_reset();
	bfree_ipc_sysv_reset();
	bfree_net_reset();
	bfree_sched_abi_reset();

	for (i = 0; i < BFREE_RLIMIT_NLIMITS; i++) {
		g_rlimits[i].rlim_cur = (uint64_t)-1;
		g_rlimits[i].rlim_max = (uint64_t)-1;
	}
	g_rlimits_init = 1;
	g_stub_n = 0;
	g_memfd_seq = 0;
	memset(g_eventfd_used, 0, sizeof(g_eventfd_used));
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

static long sysret_long(long rc)
{
	if (rc < 0 && rc > -4096)
		return rc;
	return rc;
}

static void ensure_rlimits(void)
{
	unsigned i;

	if (g_rlimits_init)
		return;
	for (i = 0; i < BFREE_RLIMIT_NLIMITS; i++) {
		g_rlimits[i].rlim_cur = (uint64_t)-1;
		g_rlimits[i].rlim_max = (uint64_t)-1;
	}
	g_rlimits_init = 1;
}

static int stub_alloc_fd(void)
{
	int fd;

	if (g_stub_n >= BFREE_STUB_FD_MAX)
		return -EMFILE;
	fd = BFREE_STUB_FD_BASE + g_stub_n;
	g_stub_fds[g_stub_n++] = fd;
	return fd;
}

static int eventfd_alloc(unsigned initval)
{
	int i;

	for (i = 0; i < BFREE_EVENTFD_MAX; i++) {
		if (!g_eventfd_used[i]) {
			g_eventfd_used[i] = 1;
			g_eventfd_cnt[i] = initval;
			return BFREE_STUB_FD_BASE + 64 + i;
		}
	}
	return -EMFILE;
}

static void fill_clock(struct guest_timespec *ts)
{
	unsigned long ticks = bfree_sched_ticks(&guest.proc);
	long sec, nsec;

	if (ts == NULL)
		return;
	if (bfree_sched_abi_time(&sec, &nsec)) {
		ts->tv_sec = sec;
		ts->tv_nsec = nsec;
		return;
	}
	ts->tv_sec = (long)(ticks / 100);
	ts->tv_nsec = (long)((ticks % 100) * 10000000UL);
}

static void nanosleep_ticks(const struct guest_timespec *req)
{
	unsigned long n;
	unsigned long i;

	if (req == NULL)
		return;
	n = 1;
	if (req->tv_sec > 0)
		n += (unsigned long)req->tv_sec;
	if (req->tv_nsec > 0)
		n += (unsigned long)(req->tv_nsec / 1000000L) + 1;
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++)
		bfree_sched_tick(&guest.proc);
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

int sys_dup3(int oldfd, int newfd, int flags)
{
	int rc;

	if (oldfd == newfd)
		return -EINVAL;
	rc = guest_dup2(&guest.io, oldfd, newfd);
	if (rc < 0)
		return rc;
	if (flags & O_CLOEXEC)
		guest_fcntl(&guest.io, newfd, 2 /* F_SETFD */, BFREE_FD_CLOEXEC);
	return newfd;
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
	if (self == NULL)
		return (void *)(intptr_t)-ESRCH;
	return bfree_mmap(&self->as, NULL, len, prot, flags);
}

int sys_mprotect(void *addr, size_t len, int prot)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return -ESRCH;
	return bfree_mprotect(&self->as, addr, len, prot);
}

int sys_munmap(void *addr, size_t len)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return -ESRCH;
	return bfree_munmap(&self->as, addr, len);
}

int sys_madvise(void *addr, size_t len, int advice)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return -ESRCH;
	return bfree_madvise(&self->as, addr, len, advice);
}

void *sys_mremap(void *old_addr, size_t old_size, size_t new_size, int flags,
		 void *new_addr)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);
	void *p;

	(void)flags;
	(void)new_addr;
	if (self == NULL)
		return (void *)(intptr_t)-ESRCH;
	if (new_size == old_size)
		return old_addr;
	p = bfree_mmap(&self->as, NULL, new_size, 0, BFREE_MMAP_ANONYMOUS |
						     BFREE_MMAP_PRIVATE);
	if ((intptr_t)p < 0 && (intptr_t)p > -4096)
		return p;
	if (old_addr != NULL && old_size > 0) {
		size_t n = old_size < new_size ? old_size : new_size;

		memcpy(p, old_addr, n);
		bfree_munmap(&self->as, old_addr, old_size);
	}
	return p;
}

int sys_msync(void *addr, size_t len, int flags)
{
	(void)addr;
	(void)len;
	(void)flags;
	return 0;
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
	unsigned char *si;

	rc = bfree_waitid(&guest.proc, idtype, id, &status, options);
	if (rc < 0)
		return rc;
	if (siginfo != NULL && rc > 0) {
		/* Linux x86_64 siginfo_t subset used by waitid. */
		si = (unsigned char *)siginfo;
		memset(si, 0, 128);
		*(int *)(si + 0) = BFREE_SIGCHLD;          /* si_signo */
		*(int *)(si + 4) = 0;                      /* si_errno */
		*(int *)(si + 8) = BFREE_CLD_EXITED;       /* si_code */
		*(int *)(si + 16) = rc;                    /* si_pid */
		*(int *)(si + 20) = 0;                     /* si_uid */
		*(int *)(si + 24) = status;                /* si_status */
	}
	return 0;
}

int sys_fork(void)
{
	return bfree_fork(&guest.proc);
}

int sys_pipe(int pipefd[2])
{
	return bfree_pipe_open(&guest.proc, pipefd);
}

int sys_pipe2(int pipefd[2], int flags)
{
	int rc;

	rc = bfree_pipe_open(&guest.proc, pipefd);
	if (rc < 0)
		return rc;
	if (flags & O_CLOEXEC) {
		guest_fcntl(&guest.io, pipefd[0], 2, BFREE_FD_CLOEXEC);
		guest_fcntl(&guest.io, pipefd[1], 2, BFREE_FD_CLOEXEC);
	}
	return 0;
}

int sys_kill(int pid, int sig)
{
	bfree_kill(&guest.proc, pid, sig);
	(void)bfree_rt_signal_poll_deliver(&guest.proc);
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
	return bfree_futex_on(&guest.proc, uaddr, op, val, timeout);
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

int sys_getpgrp(void)
{
	return sys_getpgid(0);
}

int sys_getsid(int pid)
{
	struct bfree_proc *self;
	int i;

	if (pid == 0) {
		self = bfree_proc_current(&guest.proc);
		if (self == NULL)
			return -ESRCH;
		return self->sid ? self->sid : self->pid;
	}
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (guest.proc.procs[i].state != BFREE_PROC_FREE &&
		    guest.proc.procs[i].pid == pid)
			return guest.proc.procs[i].sid
				       ? guest.proc.procs[i].sid
				       : guest.proc.procs[i].pid;
	}
	return -ESRCH;
}

int sys_gettid(void)
{
	return sys_getpid();
}

int sys_rt_sigaction(int sig, const void *act, void *oact, size_t sigsetsize)
{
	return bfree_rt_sigaction(&guest.proc, sig, act, oact, sigsetsize);
}

int sys_rt_sigprocmask(int how, const void *set, void *oset, size_t sigsetsize)
{
	return bfree_rt_sigprocmask(&guest.proc, how, set, oset, sigsetsize);
}

long sys_rt_sigreturn(void)
{
	return bfree_rt_sigreturn(&guest.proc);
}

int sys_capget(void *hdrp, void *datap)
{
	return bfree_capget(hdrp, datap);
}

int sys_capset(void *hdrp, const void *datap)
{
	return bfree_capset(hdrp, datap);
}

int sys_prlimit64(int pid, unsigned int resource, const void *new_rlim,
		  void *old_rlim)
{
	const struct guest_rlimit64 *nr = new_rlim;
	struct guest_rlimit64 *or = old_rlim;

	(void)pid;
	ensure_rlimits();
	if (resource >= BFREE_RLIMIT_NLIMITS)
		return -EINVAL;
	if (or != NULL)
		*or = g_rlimits[resource];
	if (nr != NULL)
		g_rlimits[resource] = *nr;
	return 0;
}

int sys_getrlimit(unsigned int resource, void *rlim)
{
	return sys_prlimit64(0, resource, NULL, rlim);
}

int sys_setrlimit(unsigned int resource, const void *rlim)
{
	return sys_prlimit64(0, resource, rlim, NULL);
}

int sys_prctl(int option, unsigned long a2, unsigned long a3, unsigned long a4,
	      unsigned long a5)
{
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	/* Common prctl options accepted as no-ops. */
	switch (option) {
	case 1:  /* PR_SET_PDEATHSIG */
	case 2:  /* PR_GET_PDEATHSIG */
	case 3:  /* PR_GET_DUMPABLE */
	case 4:  /* PR_SET_DUMPABLE */
	case 8:  /* PR_SET_NAME */
	case 15: /* PR_SET_NAME (alt) */
	case 16: /* PR_GET_NAME */
	case 22: /* PR_SET_SECCOMP */
	case 25: /* PR_GET_TSC */
	case 26: /* PR_SET_TSC */
	case 35: /* PR_SET_TIMERSLACK */
	case 36: /* PR_GET_TIMERSLACK */
	case 38: /* PR_SET_NO_NEW_PRIVS */
	case 39: /* PR_GET_NO_NEW_PRIVS */
	case 47: /* PR_SET_CHILD_SUBREAPER */
	case 48: /* PR_GET_CHILD_SUBREAPER */
		return 0;
	default:
		return -EINVAL;
	}
}

int sys_ioctl(int fd, unsigned long req, void *arg)
{
	(void)fd;
	switch (req) {
	case 0x5401: /* TCGETS */
		if (arg != NULL) {
			/* Minimal Linux struct termios (36 bytes on x86_64). */
			unsigned int *t = (unsigned int *)arg;
			unsigned char *cc;

			memset(arg, 0, 36);
			t[0] = 0x00000100; /* c_iflag: ICRNL-ish */
			t[1] = 0x00000005; /* c_oflag: OPOST|ONLCR */
			t[2] = 0x00000bf;  /* c_cflag: CS8|CREAD|B38400-ish */
			t[3] = 0x00008a3b; /* c_lflag: ICANON|ECHO|ISIG… */
			cc = (unsigned char *)arg + 17;
			cc[0] = 3;  /* VINTR  ^C */
			cc[1] = 28; /* VQUIT  ^\ */
			cc[2] = 127; /* VERASE DEL */
			cc[3] = 21; /* VKILL  ^U */
			cc[4] = 4;  /* VEOF   ^D */
			cc[5] = 0;  /* VTIME */
			cc[6] = 1;  /* VMIN */
		}
		return 0;
	case 0x5402: /* TCSETS */
	case 0x5403: /* TCSETSW */
	case 0x5404: /* TCSETSF */
	case 0x540B: /* TCFLSH */
	case 0x540A: /* TCSBRK */
		(void)arg;
		return 0;
	case 0x5413: /* TIOCGWINSZ */
		if (arg != NULL) {
			unsigned short *ws = arg;
			ws[0] = 24; /* row */
			ws[1] = 80; /* col */
			ws[2] = 0;
			ws[3] = 0;
		}
		return 0;
	case 0x5414: /* TIOCSWINSZ */
		(void)arg;
		return 0;
	case 0x540F: /* TIOCGPGRP */
		return bfree_tcgetpgrp(0);
	case 0x5410: /* TIOCSPGRP */
		if (arg == NULL)
			return -EFAULT;
		return bfree_tcsetpgrp(0, *(int *)arg);
	default:
		return -EINVAL;
	}
}

int sys_nanosleep(const void *req, void *rem)
{
	const struct guest_timespec *ts = req;
	struct guest_timespec *r = rem;

	if (ts == NULL)
		return -EFAULT;
	nanosleep_ticks(ts);
	if (r != NULL) {
		r->tv_sec = 0;
		r->tv_nsec = 0;
	}
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
	bfree_sched_tick(&guest.proc);
	return bfree_poll(&guest.proc, &guest.fs, fds, nfds, timeout);
}

static int fd_isset(const unsigned long *set, int fd)
{
	if (set == NULL || fd < 0)
		return 0;
	return (set[fd / (8 * (int)sizeof(unsigned long))] >>
		(fd % (8 * (int)sizeof(unsigned long)))) &
	       1UL;
}

static void fd_setbit(unsigned long *set, int fd)
{
	if (set == NULL || fd < 0)
		return;
	set[fd / (8 * (int)sizeof(unsigned long))] |=
		1UL << (fd % (8 * (int)sizeof(unsigned long)));
}

static void fd_clrall(unsigned long *set, int nfds)
{
	int nwords;

	if (set == NULL || nfds <= 0)
		return;
	nwords = (nfds + (int)(8 * sizeof(unsigned long)) - 1) /
		 (int)(8 * sizeof(unsigned long));
	memset(set, 0, (size_t)nwords * sizeof(unsigned long));
}

int sys_select(int nfds, unsigned long *readfds, unsigned long *writefds,
	       unsigned long *exceptfds, struct guest_timeval *timeout)
{
	struct bfree_pollfd pfds[64];
	unsigned int n = 0;
	int i;
	int to_ms = -1;
	int rc;
	unsigned long rcopy[16], wcopy[16], ecopy[16];

	(void)exceptfds;
	if (nfds < 0)
		return -EINVAL;
	if (nfds > 64)
		nfds = 64;
	if (timeout != NULL) {
		to_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
		if (to_ms < 0)
			to_ms = 0;
	}
	for (i = 0; i < nfds; i++) {
		short ev = 0;

		if (fd_isset(readfds, i))
			ev |= BFREE_POLLIN;
		if (fd_isset(writefds, i))
			ev |= BFREE_POLLOUT;
		if (ev == 0)
			continue;
		pfds[n].fd = i;
		pfds[n].events = ev;
		pfds[n].revents = 0;
		n++;
	}
	bfree_sched_tick(&guest.proc);
	rc = bfree_poll(&guest.proc, &guest.fs, pfds, n, to_ms);
	if (rc < 0)
		return rc;
	memset(rcopy, 0, sizeof(rcopy));
	memset(wcopy, 0, sizeof(wcopy));
	memset(ecopy, 0, sizeof(ecopy));
	for (i = 0; i < (int)n; i++) {
		if (pfds[i].revents & BFREE_POLLIN)
			fd_setbit(rcopy, pfds[i].fd);
		if (pfds[i].revents & BFREE_POLLOUT)
			fd_setbit(wcopy, pfds[i].fd);
	}
	fd_clrall(readfds, nfds);
	fd_clrall(writefds, nfds);
	fd_clrall(exceptfds, nfds);
	for (i = 0; i < nfds; i++) {
		if (fd_isset(rcopy, i))
			fd_setbit(readfds, i);
		if (fd_isset(wcopy, i))
			fd_setbit(writefds, i);
	}
	(void)ecopy;
	return rc;
}

ssize_t sys_pread64(int fd, void *buf, size_t count, off_t offset)
{
	off_t cur;
	ssize_t n;

	cur = guest_lseek(&guest.io, fd, 0, SEEK_CUR);
	if (cur < 0)
		return cur;
	if (guest_lseek(&guest.io, fd, offset, SEEK_SET) < 0)
		return -EINVAL;
	n = guest_read(&guest.io, fd, buf, count);
	guest_lseek(&guest.io, fd, cur, SEEK_SET);
	return n;
}

ssize_t sys_pwrite64(int fd, const void *buf, size_t count, off_t offset)
{
	off_t cur;
	ssize_t n;

	cur = guest_lseek(&guest.io, fd, 0, SEEK_CUR);
	if (cur < 0)
		return cur;
	if (guest_lseek(&guest.io, fd, offset, SEEK_SET) < 0)
		return -EINVAL;
	n = guest_write(&guest.io, fd, buf, count);
	guest_lseek(&guest.io, fd, cur, SEEK_SET);
	return n;
}

ssize_t sys_readv(int fd, const struct guest_iovec *iov, int iovcnt)
{
	ssize_t total = 0;
	int i;

	if (iov == NULL || iovcnt < 0)
		return -EINVAL;
	for (i = 0; i < iovcnt; i++) {
		ssize_t n;

		if (iov[i].iov_len == 0)
			continue;
		n = guest_read(&guest.io, fd, iov[i].iov_base, iov[i].iov_len);
		if (n < 0)
			return total > 0 ? total : n;
		total += n;
		if ((size_t)n < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t sys_writev(int fd, const struct guest_iovec *iov, int iovcnt)
{
	ssize_t total = 0;
	int i;

	if (iov == NULL || iovcnt < 0)
		return -EINVAL;
	for (i = 0; i < iovcnt; i++) {
		ssize_t n;

		if (iov[i].iov_len == 0)
			continue;
		n = guest_write(&guest.io, fd, iov[i].iov_base, iov[i].iov_len);
		if (n < 0)
			return total > 0 ? total : n;
		total += n;
		if ((size_t)n < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t sys_preadv(int fd, const struct guest_iovec *iov, int iovcnt,
		   off_t offset)
{
	off_t cur;
	ssize_t n;

	cur = guest_lseek(&guest.io, fd, 0, SEEK_CUR);
	if (cur < 0)
		return cur;
	if (guest_lseek(&guest.io, fd, offset, SEEK_SET) < 0)
		return -EINVAL;
	n = sys_readv(fd, iov, iovcnt);
	guest_lseek(&guest.io, fd, cur, SEEK_SET);
	return n;
}

ssize_t sys_pwritev(int fd, const struct guest_iovec *iov, int iovcnt,
		    off_t offset)
{
	off_t cur;
	ssize_t n;

	cur = guest_lseek(&guest.io, fd, 0, SEEK_CUR);
	if (cur < 0)
		return cur;
	if (guest_lseek(&guest.io, fd, offset, SEEK_SET) < 0)
		return -EINVAL;
	n = sys_writev(fd, iov, iovcnt);
	guest_lseek(&guest.io, fd, cur, SEEK_SET);
	return n;
}

int sys_uname(struct guest_utsname *buf)
{
	if (buf == NULL)
		return -EFAULT;
	memset(buf, 0, sizeof(*buf));
	memcpy(buf->sysname, "Linux", 5);
	memcpy(buf->nodename, "bfree", 5);
	memcpy(buf->release, "6.1.0-bfree", 11);
	memcpy(buf->version, "#1 SMP B-Free", 13);
	memcpy(buf->machine, "x86_64", 6);
	return 0;
}

int sys_gettimeofday(struct guest_timeval *tv, void *tz)
{
	struct guest_timespec ts;

	(void)tz;
	if (tv == NULL)
		return -EFAULT;
	fill_clock(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
	return 0;
}

long sys_time(long *tloc)
{
	struct guest_timespec ts;

	fill_clock(&ts);
	if (tloc != NULL)
		*tloc = ts.tv_sec;
	return ts.tv_sec;
}

int sys_clock_gettime(int clockid, struct guest_timespec *tp)
{
	(void)clockid;
	if (tp == NULL)
		return -EFAULT;
	fill_clock(tp);
	return 0;
}

int sys_clock_getres(int clockid, struct guest_timespec *res)
{
	(void)clockid;
	if (res == NULL)
		return -EFAULT;
	res->tv_sec = 0;
	res->tv_nsec = 10000000L;
	return 0;
}

int sys_clock_nanosleep(int clockid, int flags, const struct guest_timespec *req,
			struct guest_timespec *rem)
{
	(void)clockid;
	(void)flags;
	return sys_nanosleep(req, rem);
}

int sys_set_tid_address(int *tidptr)
{
	struct bfree_proc *self = bfree_proc_current(&guest.proc);

	if (self == NULL)
		return -ESRCH;
	self->clear_child_tid = tidptr;
	self->clear_tid_addr_set = tidptr != NULL;
	return self->pid;
}

int sys_getrusage(int who, void *usage)
{
	(void)who;
	if (usage != NULL)
		memset(usage, 0, 144);
	return 0;
}

int sys_sysinfo(void *info)
{
	if (info != NULL)
		memset(info, 0, 128);
	return 0;
}

int sys_arch_prctl(int code, unsigned long addr)
{
	unsigned long *p;

	if (code == 0x1002) { /* ARCH_SET_FS */
		g_arch_fsbase = addr;
#ifdef BFREE_KERNEL_GUEST
		/* IA32_FS_BASE — required for glibc/musl TLS. */
		{
			unsigned lo = (unsigned)addr;
			unsigned hi = (unsigned)(addr >> 32);

			__asm__ volatile("wrmsr"
					 :
					 : "c"(0xC0000100u), "a"(lo), "d"(hi)
					 : "memory");
		}
#endif
		return 0;
	}
	if (code == 0x1003) { /* ARCH_GET_FS */
		p = (unsigned long *)addr;
		if (p == NULL)
			return -EFAULT;
		*p = g_arch_fsbase;
		return 0;
	}
	return -EINVAL;
}

int sys_pause(void)
{
	bfree_sched_tick(&guest.proc);
	return -EINTR;
}

unsigned int sys_alarm(unsigned int seconds)
{
	unsigned int old = g_alarm_sec;

	g_alarm_sec = seconds;
	return old;
}

int sys_getitimer(int which, void *curr_value)
{
	struct guest_timeval *tv = curr_value;

	(void)which;
	if (tv == NULL)
		return -EFAULT;
	tv[0] = g_itimer_value;
	tv[1] = g_itimer_interval;
	return 0;
}

int sys_setitimer(int which, const void *new_value, void *old_value)
{
	const struct guest_timeval *nv = new_value;
	struct guest_timeval *ov = old_value;

	(void)which;
	if (ov != NULL) {
		ov[0] = g_itimer_value;
		ov[1] = g_itimer_interval;
	}
	if (nv != NULL) {
		g_itimer_value = nv[0];
		g_itimer_interval = nv[1];
	}
	return 0;
}

int sys_sync(void)
{
	if (guest.fs.mounted)
		return bfree_fs_sync(&guest.fs);
	return 0;
}

int sys_chroot(const char *path)
{
	/* Minimal: treat as chdir into the new root-like cwd. */
	return bfree_chdir(&guest.fs, path);
}

long sys_times(void *buf)
{
	unsigned long ticks = bfree_sched_ticks(&guest.proc);

	if (buf != NULL)
		memset(buf, 0, 32);
	return (long)ticks;
}

int sys_getpriority(int which, int who)
{
	(void)which;
	(void)who;
	return 0;
}

int sys_setpriority(int which, int who, int prio)
{
	(void)which;
	(void)who;
	(void)prio;
	return 0;
}

int sys_statfs(const char *path, struct guest_statfs *buf)
{
	(void)path;
	if (buf == NULL)
		return -EFAULT;
	memset(buf, 0, sizeof(*buf));
	buf->f_type = 0xBFEE;
	buf->f_bsize = 4096;
	buf->f_blocks = 1024;
	buf->f_bfree = 512;
	buf->f_bavail = 512;
	buf->f_files = 1024;
	buf->f_ffree = 512;
	buf->f_namelen = BFREE_MAX_NAME;
	buf->f_frsize = 4096;
	return 0;
}

int sys_fstatfs(int fd, struct guest_statfs *buf)
{
	(void)fd;
	return sys_statfs("/", buf);
}

ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	char tmp[256];
	size_t left = count;
	ssize_t total = 0;
	off_t saved = -1;

	if (offset != NULL) {
		saved = guest_lseek(&guest.io, in_fd, 0, SEEK_CUR);
		if (guest_lseek(&guest.io, in_fd, *offset, SEEK_SET) < 0)
			return -EINVAL;
	}
	while (left > 0) {
		size_t chunk = left < sizeof(tmp) ? left : sizeof(tmp);
		ssize_t n = guest_read(&guest.io, in_fd, tmp, chunk);
		ssize_t w;

		if (n <= 0)
			break;
		w = guest_write(&guest.io, out_fd, tmp, (size_t)n);
		if (w < 0) {
			if (offset != NULL && saved >= 0)
				guest_lseek(&guest.io, in_fd, saved, SEEK_SET);
			return total > 0 ? total : w;
		}
		total += w;
		left -= (size_t)w;
		if (w < n)
			break;
	}
	if (offset != NULL) {
		*offset += total;
		if (saved >= 0)
			guest_lseek(&guest.io, in_fd, saved, SEEK_SET);
	}
	return total;
}

int sys_fallocate(int fd, int mode, off_t offset, off_t len)
{
	struct bfree_linux_stat st;
	off_t need;
	int rc;

	(void)mode;
	rc = bfree_fstat(&guest.fs, fd, &st);
	if (rc < 0)
		return rc;
	need = offset + len;
	if (need > st.st_size)
		return bfree_ftruncate(&guest.fs, fd, need);
	return 0;
}

int sys_getcpu(unsigned *cpu, unsigned *node, void *tcache)
{
	(void)tcache;
	if (cpu != NULL)
		*cpu = 0;
	if (node != NULL)
		*node = 0;
	return 0;
}

int sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
	unsigned char *p = buf;
	unsigned long tick;
	size_t i;

	(void)flags;
	if (buf == NULL)
		return -EFAULT;
	tick = bfree_sched_ticks(&guest.proc);
	for (i = 0; i < buflen; i++) {
		tick = tick * 1103515245UL + 12345UL + (unsigned long)i;
		p[i] = (unsigned char)(tick >> 16);
	}
	return (int)buflen;
}

int sys_statx(int dirfd, const char *path, int flags, unsigned int mask,
	      void *statxbuf)
{
	struct bfree_linux_stat st;
	int rc;

	(void)mask;
	if (statxbuf == NULL)
		return -EFAULT;
	rc = bfree_fstatat(&guest.fs, dirfd, path, &st, flags);
	if (rc < 0)
		return rc;
	memset(statxbuf, 0, 256);
	/* Copy linux_stat fields into the leading region of the buffer. */
	memcpy(statxbuf, &st, sizeof(st) < 256 ? sizeof(st) : 256);
	return 0;
}

int sys_memfd_create(const char *name, unsigned int flags)
{
	char path[BFREE_MAX_PATH];
	size_t i;
	int fd;

	(void)flags;
	g_memfd_seq++;
	memcpy(path, "/tmp/memfd_", 11);
	i = 11;
	{
		unsigned n = (unsigned)g_memfd_seq;
		char tmp[16];
		int t = 0;

		if (n == 0)
			tmp[t++] = '0';
		while (n > 0 && t < 15) {
			tmp[t++] = (char)('0' + (n % 10));
			n /= 10;
		}
		while (t > 0 && i + 1 < sizeof(path))
			path[i++] = tmp[--t];
	}
	if (name != NULL) {
		path[i++] = '_';
		while (*name && i + 1 < sizeof(path))
			path[i++] = *name++;
	}
	path[i] = '\0';
	bfree_unlink(&guest.fs, path);
	fd = bfree_open(&guest.fs, path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
	if (fd < 0)
		return fd;
	bfree_close(&guest.fs, fd);
	return bfree_open(&guest.fs, path, 2 /* O_RDWR */, 0);
}

int sys_execveat(int dirfd, const char *path, char *const argv[],
		 char *const envp[], int flags)
{
	char resolved[BFREE_MAX_PATH];
	struct bfree_linux_stat st;
	int rc;

	(void)flags;
	if (path == NULL)
		return -EFAULT;
	if (path[0] == '/' || dirfd == BFREE_AT_FDCWD)
		return bfree_execve(&guest.proc, path, (char **)argv,
				    (char **)envp);
	rc = bfree_fstatat(&guest.fs, dirfd, path, &st, 0);
	if (rc < 0)
		return rc;
	/* Best-effort: prefer absolute path when path is already absolute-like. */
	if (path[0] == '.') {
		if (bfree_getcwd(&guest.fs, resolved, sizeof(resolved)) < 0)
			return -ENOENT;
		return bfree_execve(&guest.proc, path, (char **)argv,
				    (char **)envp);
	}
	return bfree_execve(&guest.proc, path, (char **)argv, (char **)envp);
}

int sys_utimensat(int dirfd, const char *path, const struct guest_timespec *times,
		  int flags)
{
	uint64_t t4[4];

	if (times != NULL) {
		t4[0] = (uint64_t)times[0].tv_sec;
		t4[1] = (uint64_t)times[0].tv_nsec;
		t4[2] = (uint64_t)times[1].tv_sec;
		t4[3] = (uint64_t)times[1].tv_nsec;
		return bfree_utimensat(&guest.fs, dirfd, path, t4, flags);
	}
	return bfree_utimensat(&guest.fs, dirfd, path, NULL, flags);
}

long bfree_invoke_syscall(unsigned long nr, unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3, unsigned long a4,
			  unsigned long a5)
{
	if (!bfree_syscall_is_implemented(nr))
		return -ENOSYS;

	switch (nr) {
	case 0: /* read */
		return sysret_long(sys_read((int)a0, (void *)a1, (size_t)a2));
	case 1: /* write */
		return sysret_long(sys_write((int)a0, (const void *)a1,
					     (size_t)a2));
	case 2: /* open */
		return sys_open((const char *)a0, (int)a1, (int)a2);
	case 3: /* close */
		return sys_close((int)a0);
	case 4: /* stat */
		return sys_stat((const char *)a0,
				(struct bfree_linux_stat *)a1);
	case 5: /* fstat */
		return sys_fstat((int)a0, (struct bfree_linux_stat *)a1);
	case 6: /* lstat */
		return sys_stat((const char *)a0,
				(struct bfree_linux_stat *)a1);
	case 7: /* poll */
		return sys_poll((struct bfree_pollfd *)a0, (unsigned int)a1,
				(int)a2);
	case 8: /* lseek */
		return sysret_long(sys_lseek((int)a0, (off_t)a1, (int)a2));
	case 9: /* mmap */
		return (long)(uintptr_t)sys_mmap((void *)a0, (size_t)a1,
						 (int)a2, (int)a3, (int)a4,
						 (off_t)a5);
	case 10: /* mprotect */
		return sys_mprotect((void *)a0, (size_t)a1, (int)a2);
	case 11: /* munmap */
		return sys_munmap((void *)a0, (size_t)a1);
	case 12: /* brk */
		return (long)sys_brk((uintptr_t)a0);
	case 13: /* rt_sigaction */
		return sys_rt_sigaction((int)a0, (const void *)a1, (void *)a2,
					(size_t)a3);
	case 14: /* rt_sigprocmask */
		return sys_rt_sigprocmask((int)a0, (const void *)a1, (void *)a2,
					  (size_t)a3);
	case 15: /* rt_sigreturn */
		return sys_rt_sigreturn();
	case 16: /* ioctl */
		return sys_ioctl((int)a0, a1, (void *)a2);
	case 17: /* pread64 */
		return sysret_long(sys_pread64((int)a0, (void *)a1, (size_t)a2,
					       (off_t)a3));
	case 18: /* pwrite64 */
		return sysret_long(sys_pwrite64((int)a0, (const void *)a1,
						(size_t)a2, (off_t)a3));
	case 19: /* readv */
		return sysret_long(sys_readv((int)a0,
					     (const struct guest_iovec *)a1,
					     (int)a2));
	case 20: /* writev */
		return sysret_long(sys_writev((int)a0,
					      (const struct guest_iovec *)a1,
					      (int)a2));
	case 21: /* access */
		return bfree_access(&guest.fs, (const char *)a0, (int)a1);
	case 22: /* pipe */
		return sys_pipe((int *)a0);
	case 23: /* select */
		return sys_select((int)a0, (unsigned long *)a1,
				  (unsigned long *)a2, (unsigned long *)a3,
				  (struct guest_timeval *)a4);
	case 24: /* sched_yield */
		return bfree_sched_yield(&guest.proc);
	case 25: /* mremap */
		return (long)(uintptr_t)sys_mremap((void *)a0, (size_t)a1,
						   (size_t)a2, (int)a3,
						   (void *)a4);
	case 26: /* msync */
		return sys_msync((void *)a0, (size_t)a1, (int)a2);
	case 28: /* madvise */
		return sys_madvise((void *)a0, (size_t)a1, (int)a2);
	case 29: /* shmget */
		return sys_shmget((int)a0, (unsigned long)a1, (int)a2);
	case 30: /* shmat */
		return (long)(uintptr_t)sys_shmat((int)a0, (const void *)a1,
						  (int)a2);
	case 31: /* shmctl */
		return sys_shmctl((int)a0, (int)a1,
				  (struct bfree_shmid_ds *)a2);
	case 32: /* dup */
		return sys_dup((int)a0);
	case 33: /* dup2 */
		return sys_dup2((int)a0, (int)a1);
	case 34: /* pause */
		return sys_pause();
	case 35: /* nanosleep */
		return sys_nanosleep((const void *)a0, (void *)a1);
	case 36: /* getitimer */
		return sys_getitimer((int)a0, (void *)a1);
	case 37: /* alarm */
		return (long)sys_alarm((unsigned int)a0);
	case 38: /* setitimer */
		return sys_setitimer((int)a0, (const void *)a1, (void *)a2);
	case 39: /* getpid */
		return sys_getpid();
	case 40: /* sendfile */
		return sysret_long(sys_sendfile((int)a0, (int)a1, (off_t *)a2,
						(size_t)a3));
	case 41: /* socket */
		return sys_socket((int)a0, (int)a1, (int)a2);
	case 42: /* connect */
		return sys_connect((int)a0,
				   (const struct bfree_sockaddr_un *)a1,
				   (unsigned int)a2);
	case 43: /* accept */
	{
		int rc = bfree_accept((int)a0, (struct bfree_sockaddr_un *)a1,
				      (unsigned int *)a2);
		if (rc == -EAGAIN) {
			bfree_sched_tick(&guest.proc);
			rc = bfree_accept((int)a0,
					  (struct bfree_sockaddr_un *)a1,
					  (unsigned int *)a2);
		}
		return rc;
	}
	case 44: /* sendto */
		return sysret_long(sys_sendto(
			(int)a0, (const void *)a1, (unsigned int)a2, (int)a3,
			(const struct bfree_sockaddr_un *)a4,
			(unsigned int)a5));
	case 45: /* recvfrom */
	{
		long rc = sysret_long(sys_recvfrom(
			(int)a0, (void *)a1, (unsigned int)a2, (int)a3,
			(struct bfree_sockaddr_un *)a4, (unsigned int *)a5));
		if (rc == -EAGAIN) {
			bfree_sched_tick(&guest.proc);
			rc = sysret_long(sys_recvfrom(
				(int)a0, (void *)a1, (unsigned int)a2, (int)a3,
				(struct bfree_sockaddr_un *)a4,
				(unsigned int *)a5));
		}
		return rc;
	}
	case 46: /* sendmsg */
		return sysret_long(bfree_sendmsg((int)a0, (const void *)a1,
						 (int)a2));
	case 47: /* recvmsg */
		return sysret_long(bfree_recvmsg((int)a0, (void *)a1, (int)a2));
	case 48: /* shutdown */
		return bfree_shutdown((int)a0, (int)a1);
	case 49: /* bind */
		return sys_bind((int)a0, (const struct bfree_sockaddr_un *)a1,
				(unsigned int)a2);
	case 50: /* listen */
		return bfree_listen((int)a0, (int)a1);
	case 51: /* getsockname */
		return bfree_getsockname((int)a0,
					 (struct bfree_sockaddr_un *)a1,
					 (unsigned int *)a2);
	case 52: /* getpeername */
		return bfree_getpeername((int)a0,
					 (struct bfree_sockaddr_un *)a1,
					 (unsigned int *)a2);
	case 53: /* socketpair */
		return sys_socketpair((int)a0, (int)a1, (int)a2, (int *)a3);
	case 54: /* setsockopt */
		return bfree_setsockopt((int)a0, (int)a1, (int)a2,
					(const void *)a3, (unsigned int)a4);
	case 55: /* getsockopt */
		return bfree_getsockopt((int)a0, (int)a1, (int)a2, (void *)a3,
					(unsigned int *)a4);
	case 56: /* clone */
		return sys_clone(a0, (void *)a1, (int *)a2, (void *)a3,
				 (int *)a4);
	case 57: /* fork */
		return sys_fork();
	case 58: /* vfork */
		return sys_vfork();
	case 59: /* execve */
		return sys_execve((const char *)a0, (char *const *)a1,
				  (char *const *)a2);
	case 60: /* exit */
		sys_exit((int)a0);
		return 0;
	case 61: /* wait4 */
		return sys_wait4((int)a0, (int *)a1, (int)a2, (void *)a3);
	case 62: /* kill */
		return sys_kill((int)a0, (int)a1);
	case 63: /* uname */
		return sys_uname((struct guest_utsname *)a0);
	case 64: /* semget */
		return bfree_semget((int)a0, (int)a1, (int)a2);
	case 65: /* semop */
		return bfree_semop((int)a0, (short *)a1, (unsigned)a2);
	case 66: /* semctl */
		return bfree_semctl((int)a0, (int)a1, (int)a2, (void *)a3);
	case 67: /* shmdt */
		return sys_shmdt((const void *)a0);
	case 68: /* msgget */
		return bfree_msgget((int)a0, (int)a1);
	case 69: /* msgsnd */
		return bfree_msgsnd((int)a0, (const void *)a1,
				    (unsigned long)a2, (int)a3);
	case 70: /* msgrcv */
		return bfree_msgrcv((int)a0, (void *)a1, (unsigned long)a2,
				    (long)a3, (int)a4);
	case 71: /* msgctl */
		return bfree_msgctl((int)a0, (int)a1,
				    (struct bfree_msqid_ds *)a2);
	case 72: /* fcntl */
		return sys_fcntl((int)a0, (int)a1, (long)a2);
	case 73: /* flock */
		return bfree_flock(&guest.fs, (int)a0, (int)a1);
	case 74: /* fsync */
		return bfree_fsync_path(&guest.fs, (int)a0);
	case 75: /* fdatasync */
		return bfree_fsync_path(&guest.fs, (int)a0);
	case 76: /* truncate */
		return bfree_truncate(&guest.fs, (const char *)a0, (off_t)a1);
	case 77: /* ftruncate */
		return bfree_ftruncate(&guest.fs, (int)a0, (off_t)a1);
	case 79: /* getcwd */
		return sys_getcwd((char *)a0, (size_t)a1);
	case 80: /* chdir */
		return sys_chdir((const char *)a0);
	case 81: /* fchdir */
		return bfree_fchdir(&guest.fs, (int)a0);
	case 82: /* rename */
		return bfree_rename(&guest.fs, (const char *)a0,
				    (const char *)a1);
	case 83: /* mkdir */
		return bfree_mkdir(&guest.fs, (const char *)a0, (int)a1);
	case 84: /* rmdir */
		return bfree_rmdir(&guest.fs, (const char *)a0);
	case 85: /* creat */
		return sys_open((const char *)a0,
				O_CREAT | O_WRONLY | O_TRUNC, (int)a1);
	case 86: /* link */
		return bfree_link(&guest.fs, (const char *)a0,
				  (const char *)a1);
	case 87: /* unlink */
		return sys_unlink((const char *)a0);
	case 88: /* symlink */
		return bfree_symlink(&guest.fs, (const char *)a0,
				     (const char *)a1);
	case 89: /* readlink */
		return sysret_long(bfree_readlink(&guest.fs, (const char *)a0,
						  (char *)a1, (size_t)a2));
	case 90: /* chmod */
		return bfree_chmod(&guest.fs, (const char *)a0,
				   (unsigned)a1);
	case 91: /* fchmod */
		return bfree_fchmod(&guest.fs, (int)a0, (unsigned)a1);
	case 92: /* chown */
		return bfree_chown(&guest.fs, (const char *)a0, (unsigned)a1,
				   (unsigned)a2);
	case 93: /* fchown */
		return bfree_fchown(&guest.fs, (int)a0, (unsigned)a1,
				    (unsigned)a2);
	case 94: /* lchown */
		return bfree_chown(&guest.fs, (const char *)a0, (unsigned)a1,
				   (unsigned)a2);
	case 95: /* umask */
		return (long)bfree_umask(&guest.fs, (unsigned)a0);
	case 96: /* gettimeofday */
		return sys_gettimeofday((struct guest_timeval *)a0, (void *)a1);
	case 97: /* getrlimit */
		return sys_getrlimit((unsigned int)a0, (void *)a1);
	case 98: /* getrusage */
		return sys_getrusage((int)a0, (void *)a1);
	case 99: /* sysinfo */
		return sys_sysinfo((void *)a0);
	case 100: /* times */
		return sys_times((void *)a0);
	case 102: /* getuid */
		return (long)sys_getuid();
	case 104: /* getgid */
		return (long)sys_getgid();
	case 105: /* setuid */
		return sys_setuid((unsigned int)a0);
	case 106: /* setgid */
		return sys_setgid((unsigned int)a0);
	case 107: /* geteuid */
		return (long)sys_geteuid();
	case 108: /* getegid */
		return (long)sys_getegid();
	case 109: /* setpgid */
		return sys_setpgid((int)a0, (int)a1);
	case 110: /* getppid */
		return sys_getppid();
	case 111: /* getpgrp */
		return sys_getpgrp();
	case 112: /* setsid */
		return sys_setsid();
	case 113: /* setreuid */
		return sys_setreuid((unsigned int)a0, (unsigned int)a1);
	case 114: /* setregid */
		return sys_setregid((unsigned int)a0, (unsigned int)a1);
	case 115: /* getgroups */
		return bfree_getgroups((int)a0, (unsigned int *)a1);
	case 116: /* setgroups */
		return bfree_setgroups((int)a0, (const unsigned int *)a1);
	case 117: /* setresuid */
		return bfree_setresuid((unsigned int)a0, (unsigned int)a1,
				       (unsigned int)a2);
	case 118: /* getresuid */
		return bfree_getresuid((unsigned int *)a0, (unsigned int *)a1,
				       (unsigned int *)a2);
	case 119: /* setresgid */
		return bfree_setresgid((unsigned int)a0, (unsigned int)a1,
				       (unsigned int)a2);
	case 120: /* getresgid */
		return bfree_getresgid((unsigned int *)a0, (unsigned int *)a1,
				       (unsigned int *)a2);
	case 121: /* getpgid */
		return sys_getpgid((int)a0);
	case 124: /* getsid */
		return sys_getsid((int)a0);
	case 125: /* capget */
		return sys_capget((void *)a0, (void *)a1);
	case 126: /* capset */
		return sys_capset((void *)a0, (const void *)a1);
	case 127: /* rt_sigpending */
	{
		struct bfree_proc *self = bfree_proc_current(&guest.proc);
		unsigned long pending = 0;

		if (a0 == 0)
			return -EFAULT;
		if (self != NULL) {
			if (self->sigint_pending)
				pending |= BFREE_SIGBIT(BFREE_SIGINT);
			if (self->sigpipe_pending)
				pending |= BFREE_SIGBIT(BFREE_SIGPIPE);
			if (self->sigchld_pending)
				pending |= BFREE_SIGBIT(BFREE_SIGCHLD);
		}
		*(unsigned long *)a0 = pending;
		return 0;
	}
	case 128: /* rt_sigtimedwait */
		bfree_sched_tick(&guest.proc);
		return -EAGAIN;
	case 129: /* rt_sigqueueinfo */
		return sys_kill((int)a0, a1 ? (int)(*(const unsigned *)a1) : 0);
	case 130: /* rt_sigsuspend */
		bfree_sched_tick(&guest.proc);
		return -EINTR;
	case 131: /* sigaltstack */
		if (a1 != 0) {
			/* old_ss: report disabled */
			*(unsigned long *)a1 = 0;
			*((unsigned long *)a1 + 1) = 0;
			*((unsigned long *)a1 + 2) = 2; /* SS_DISABLE */
		}
		(void)a0;
		return 0;
	case 132: /* utime */
		return sys_utimensat(BFREE_AT_FDCWD, (const char *)a0, NULL, 0);
	case 133: /* mknod */
		return bfree_mknodat(&guest.fs, BFREE_AT_FDCWD,
				     (const char *)a0, (unsigned)a1,
				     (unsigned)a2);
	case 137: /* statfs */
		return sys_statfs((const char *)a0, (struct guest_statfs *)a1);
	case 138: /* fstatfs */
		return sys_fstatfs((int)a0, (struct guest_statfs *)a1);
	case 140: /* getpriority */
		return sys_getpriority((int)a0, (int)a1);
	case 141: /* setpriority */
		return sys_setpriority((int)a0, (int)a1, (int)a2);
	case 142: /* sched_setparam */
		return bfree_sched_setparam((int)a0,
					    (const struct bfree_sched_param *)a1);
	case 143: /* sched_getparam */
		return bfree_sched_getparam((int)a0,
					    (struct bfree_sched_param *)a1);
	case 144: /* sched_setscheduler */
		return bfree_sched_setscheduler(
			(int)a0, (int)a1, (const struct bfree_sched_param *)a2);
	case 145: /* sched_getscheduler */
		return bfree_sched_getscheduler((int)a0);
	case 146: /* sched_get_priority_max */
		return bfree_sched_get_priority_max((int)a0);
	case 147: /* sched_get_priority_min */
		return bfree_sched_get_priority_min((int)a0);
	case 148: /* sched_rr_get_interval */
		return bfree_sched_rr_get_interval((int)a0, (void *)a1);
	case 157: /* prctl */
		return sys_prctl((int)a0, a1, a2, a3, a4);
	case 158: /* arch_prctl */
		return sys_arch_prctl((int)a0, a1);
	case 160: /* setrlimit */
		return sys_setrlimit((unsigned int)a0, (const void *)a1);
	case 161: /* chroot */
		return sys_chroot((const char *)a0);
	case 162: /* sync */
		return sys_sync();
	case 164: /* settimeofday */
		return bfree_settimeofday((const void *)a0, (const void *)a1);
	case 165: /* mount */
		return sys_mount((const char *)a0, (const char *)a1,
				 (const char *)a2, a3, (const void *)a4);
	case 166: /* umount2 */
		return sys_umount2((const char *)a0, (int)a1);
	case 186: /* gettid */
		return sys_gettid();
	case 200: /* tkill */
		return sys_kill((int)a0, (int)a1);
	case 201: /* time */
		return sys_time((long *)a0);
	case 202: /* futex */
		return sys_futex((int *)a0, (int)a1, (int)a2,
				 (const void *)a3);
	case 203: /* sched_setaffinity */
		return bfree_sched_setaffinity((int)a0, a1,
					       (const unsigned long *)a2);
	case 204: /* sched_getaffinity */
		return bfree_sched_getaffinity((int)a0, a1,
					       (unsigned long *)a2);
	case 213: /* epoll_create */
		return bfree_epoll_create1(0);
	case 217: /* getdents64 */
		return sys_getdents64((int)a0, (void *)a1, (size_t)a2);
	case 218: /* set_tid_address */
		return sys_set_tid_address((int *)a0);
	case 228: /* clock_gettime */
		return sys_clock_gettime((int)a0, (struct guest_timespec *)a1);
	case 229: /* clock_getres */
		return sys_clock_getres((int)a0, (struct guest_timespec *)a1);
	case 230: /* clock_nanosleep */
		return sys_clock_nanosleep((int)a0, (int)a1,
					   (const struct guest_timespec *)a2,
					   (struct guest_timespec *)a3);
	case 227: /* clock_settime */
		return bfree_clock_settime((int)a0, (const void *)a1);
	case 231: /* exit_group */
		sys_exit((int)a0);
		return 0;
	case 232: /* epoll_wait */
		return bfree_epoll_wait(&guest.proc, &guest.fs, (int)a0,
					(struct bfree_epoll_event *)a1,
					(int)a2, (int)a3);
	case 233: /* epoll_ctl */
		return bfree_epoll_ctl((int)a0, (int)a1, (int)a2,
				       (struct bfree_epoll_event *)a3);
	case 234: /* tgkill */
		return sys_kill((int)a1, (int)a2);
	case 235: /* utimes */
		return sys_utimensat(BFREE_AT_FDCWD, (const char *)a0, NULL, 0);
	case 247: /* waitid */
		return sys_waitid((int)a0, (int)a1, (void *)a2, (int)a3);
	case 253: /* inotify_init */
		return stub_alloc_fd();
	case 254: /* inotify_add_watch */
		(void)a0;
		(void)a1;
		(void)a2;
		return 1;
	case 255: /* inotify_rm_watch */
		(void)a0;
		(void)a1;
		return 0;
	case 257: /* openat */
		return sys_openat((int)a0, (const char *)a1, (int)a2, (int)a3);
	case 258: /* mkdirat */
		return sys_mkdirat((int)a0, (const char *)a1, (int)a2);
	case 259: /* mknodat */
		return bfree_mknodat(&guest.fs, (int)a0, (const char *)a1,
				     (unsigned)a2, (unsigned)a3);
	case 260: /* fchownat */
		return bfree_fchownat(&guest.fs, (int)a0, (const char *)a1,
				      (unsigned)a2, (unsigned)a3, (int)a4);
	case 261: /* futimesat */
		return sys_utimensat((int)a0, (const char *)a1, NULL, 0);
	case 262: /* newfstatat */
		return sys_newfstatat((int)a0, (const char *)a1,
				      (struct bfree_linux_stat *)a2, (int)a3);
	case 263: /* unlinkat */
		return sys_unlinkat((int)a0, (const char *)a1, (int)a2);
	case 264: /* renameat */
		return bfree_renameat(&guest.fs, (int)a0, (const char *)a1,
				      (int)a2, (const char *)a3);
	case 265: /* linkat */
		return bfree_linkat(&guest.fs, (int)a0, (const char *)a1,
				    (int)a2, (const char *)a3, (int)a4);
	case 266: /* symlinkat */
		return bfree_symlinkat(&guest.fs, (const char *)a0, (int)a1,
				       (const char *)a2);
	case 267: /* readlinkat */
		return sysret_long(bfree_readlinkat(&guest.fs, (int)a0,
						    (const char *)a1,
						    (char *)a2, (size_t)a3));
	case 268: /* fchmodat */
		return bfree_fchmodat(&guest.fs, (int)a0, (const char *)a1,
				      (unsigned)a2, (int)a3);
	case 269: /* faccessat */
		return bfree_faccessat(&guest.fs, (int)a0, (const char *)a1,
				       (int)a2, (int)a3);
	case 270: /* pselect6 */
		return sys_select((int)a0, (unsigned long *)a1,
				  (unsigned long *)a2, (unsigned long *)a3,
				  NULL);
	case 271: /* ppoll */
	{
		const struct guest_timespec *ts =
			(const struct guest_timespec *)a2;
		int timeout = -1;
		unsigned long old_mask = 0;
		unsigned long new_mask = 0;
		struct bfree_proc *self;
		long rc;

		if (ts != NULL) {
			long ms = ts->tv_sec * 1000L;
			long nsec_ms = (ts->tv_nsec + 999999L) / 1000000L;

			if (ms < 0)
				ms = 0x7fffffff;
			else if (nsec_ms > 0)
				ms += nsec_ms;
			if (ms > 0x7fffffff)
				ms = 0x7fffffff;
			timeout = (int)ms;
		}
		self = bfree_proc_current(&guest.proc);
		if (a3 != 0 && self != NULL) {
			old_mask = self->sig_mask;
			new_mask = *(const unsigned long *)a3;
			self->sig_mask = new_mask;
		}
		rc = sys_poll((struct bfree_pollfd *)a0, (unsigned int)a1,
			      timeout);
		if (a3 != 0 && self != NULL)
			self->sig_mask = old_mask;
		return rc;
	}
	case 272: /* unshare */
		(void)a0;
		return 0;
	case 273: /* set_robust_list */
		(void)a0;
		(void)a1;
		return 0;
	case 274: /* get_robust_list */
		if (a1 == 0 || a2 == 0)
			return -EFAULT;
		*(unsigned long *)a1 = 0;
		*(size_t *)a2 = 0;
		(void)a0;
		return 0;
	case 280: /* utimensat */
		return sys_utimensat((int)a0, (const char *)a1,
				     (const struct guest_timespec *)a2,
				     (int)a3);
	case 281: /* epoll_pwait */
		return bfree_epoll_wait(&guest.proc, &guest.fs, (int)a0,
					(struct bfree_epoll_event *)a1,
					(int)a2, (int)a3);
	case 283: /* timerfd_create */
		(void)a0;
		(void)a1;
		return stub_alloc_fd();
	case 284: /* eventfd */
		return eventfd_alloc((unsigned)a0);
	case 285: /* fallocate */
		return sys_fallocate((int)a0, (int)a1, (off_t)a2, (off_t)a3);
	case 286: /* timerfd_settime */
		(void)a0;
		(void)a1;
		(void)a2;
		(void)a3;
		return 0;
	case 287: /* timerfd_gettime */
		if (a1 == 0)
			return -EFAULT;
		memset((void *)a1, 0, 32);
		(void)a0;
		return 0;
	case 288: /* accept4 */
		return bfree_accept((int)a0, (struct bfree_sockaddr_un *)a1,
				    (unsigned int *)a2);
	case 289: /* signalfd4 */
		(void)a0;
		(void)a1;
		(void)a2;
		(void)a3;
		return stub_alloc_fd();
	case 290: /* eventfd2 */
		return eventfd_alloc((unsigned)a0);
	case 291: /* epoll_create1 */
		return bfree_epoll_create1((int)a0);
	case 292: /* dup3 */
		return sys_dup3((int)a0, (int)a1, (int)a2);
	case 293: /* pipe2 */
		return sys_pipe2((int *)a0, (int)a1);
	case 294: /* inotify_init1 */
		(void)a0;
		return stub_alloc_fd();
	case 295: /* preadv */
		return sysret_long(sys_preadv((int)a0,
					      (const struct guest_iovec *)a1,
					      (int)a2, (off_t)a3));
	case 296: /* pwritev */
		return sysret_long(sys_pwritev((int)a0,
					       (const struct guest_iovec *)a1,
					       (int)a2, (off_t)a3));
	case 299: /* recvmmsg */
		return sysret_long(bfree_recvmsg((int)a0, (void *)a1, (int)a4));
	case 302: /* prlimit64 */
		return sys_prlimit64((int)a0, (unsigned int)a1,
				     (const void *)a2, (void *)a3);
	case 307: /* sendmmsg */
		return sysret_long(bfree_sendmsg((int)a0, (const void *)a1,
						 (int)a3));
	case 309: /* getcpu */
		return sys_getcpu((unsigned *)a0, (unsigned *)a1, (void *)a2);
	case 316: /* renameat2 */
		return bfree_renameat(&guest.fs, (int)a0, (const char *)a1,
				      (int)a2, (const char *)a3);
	case 318: /* getrandom */
		return sys_getrandom((void *)a0, (size_t)a1, (unsigned int)a2);
	case 319: /* memfd_create */
		return sys_memfd_create((const char *)a0, (unsigned int)a1);
	case 322: /* execveat */
		return sys_execveat((int)a0, (const char *)a1,
				    (char *const *)a2, (char *const *)a3,
				    (int)a4);
	case 332: /* statx */
		return sys_statx((int)a0, (const char *)a1, (int)a2,
				 (unsigned int)a3, (void *)a4);
	case 334: /* rseq */
		return bfree_rseq((void *)a0, (unsigned int)a1, (int)a2,
				  (unsigned int)a3);
	case 439: /* faccessat2 */
		return bfree_faccessat(&guest.fs, (int)a0, (const char *)a1,
				       (int)a2, (int)a3);
	default:
		return -ENOSYS;
	}
}
