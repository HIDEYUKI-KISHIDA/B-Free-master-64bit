/*
 * P30_LINUX_COMPAT — guest-fail stubs, /dev+/proc, job signals, FD/pipe/wait.
 */
#include "devnode.h"
#include "fs_ofd.h"
#include "process.h"
#include "procfs.h"
#include "signal_frame.h"
#include "syscall.h"
#include "tty.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

int main(void)
{
	struct bfree_proc *self;
	struct bfree_pollfd pfds[2];
	struct bfree_sigaction_abi act;
	struct bfree_rt_sigframe *frame;
	struct bfree_linux_stat st;
	unsigned char stackbuf[4096];
	unsigned char dirbuf[512];
	unsigned char rnd[16];
	int pipefd[2];
	int fd;
	int pgrp = -1;
	int status = -1;
	long rc;
	unsigned long sas[3];
	unsigned long old_sas[3];
	uintptr_t stack_top;

	guest_init();
	self = bfree_proc_current(guest_proc_mgr());
	CHECK(self != NULL, "current");
	CHECK(bfree_procfs_init(guest_fs()) == 0 ||
		      bfree_lookup(guest_fs(), "/proc/self") != NULL,
	      "procfs");
	bfree_devnodes_init(guest_fs());

	/* 1) poll negative fd ignored; pipe O_NONBLOCK -> EAGAIN. */
	pfds[0].fd = -1;
	pfds[0].events = BFREE_POLLIN;
	pfds[0].revents = 0x7f;
	CHECK(bfree_poll(guest_proc_mgr(), guest_fs(), pfds, 1, 0) == 0,
	      "poll ignore neg");
	CHECK(pfds[0].revents == 0, "neg revents 0");
	CHECK(bfree_pipe_open2(guest_proc_mgr(), pipefd, O_NONBLOCK) == 0,
	      "pipe2 NONBLOCK");
	rc = bfree_pipe_read(guest_proc_mgr(), pipefd[0], rnd, 1);
	CHECK(rc == -EAGAIN, "nonblock empty read");
	bfree_pipe_close(guest_proc_mgr(), pipefd[0]);
	bfree_pipe_close(guest_proc_mgr(), pipefd[1]);

	/* F_GETFL/F_SETFL on file + pipe. */
	CHECK(bfree_create(guest_fs(), "/tmp/p30f", 0644) == 0, "create");
	fd = bfree_open(guest_fs(), "/tmp/p30f", 0, 0);
	CHECK(fd >= 0, "open");
	CHECK(bfree_fcntl(guest_fs(), fd, 3 /* F_GETFL */, 0) >= 0, "F_GETFL");
	CHECK(bfree_fcntl(guest_fs(), fd, 4 /* F_SETFL */, O_NONBLOCK) == 0,
	      "F_SETFL");
	bfree_close(guest_fs(), fd);
	CHECK(bfree_pipe_open2(guest_proc_mgr(), pipefd, 0) == 0, "pipe2");
	CHECK(bfree_pipe_fcntl(guest_proc_mgr(), pipefd[0], 4, O_NONBLOCK) == 0,
	      "pipe F_SETFL");
	CHECK((bfree_pipe_fcntl(guest_proc_mgr(), pipefd[0], 3, 0) & O_NONBLOCK) !=
		      0,
	      "pipe F_GETFL NONBLOCK");
	bfree_pipe_close(guest_proc_mgr(), pipefd[0]);
	bfree_pipe_close(guest_proc_mgr(), pipefd[1]);

	/* pipe2 CLOEXEC + exec clears. */
	CHECK(bfree_pipe_open2(guest_proc_mgr(), pipefd, O_CLOEXEC) == 0,
	      "pipe2 CLOEXEC");
	CHECK((self->fd_flags[pipefd[0]] & BFREE_FD_CLOEXEC) != 0, "cloexec set");
	bfree_proc_close_cloexec(guest_proc_mgr(), guest_fs());
	CHECK(!bfree_pipe_is_fd(guest_proc_mgr(), pipefd[0]), "cloexec closed");

	/* 2/3) /dev/tty urandom + /proc/self/fd + /sys */
	CHECK(bfree_lookup(guest_fs(), "/dev/tty") != NULL, "/dev/tty");
	CHECK(bfree_lookup(guest_fs(), "/dev/urandom") != NULL, "/dev/urandom");
	fd = bfree_open(guest_fs(), "/dev/urandom", 0, 0);
	CHECK(fd >= 0, "open urandom");
	CHECK(bfree_dev_read(guest_fs(), fd, rnd, sizeof(rnd)) ==
		      (ssize_t)sizeof(rnd),
	      "urandom read");
	bfree_close(guest_fs(), fd);
	CHECK(bfree_lookup(guest_fs(), "/proc/self/fd/0") != NULL, "proc fd0");
	CHECK(bfree_lookup(guest_fs(), "/sys/devices/system/cpu") != NULL,
	      "/sys cpu");

	/* getdents emits . and .. */
	CHECK(bfree_mkdir(guest_fs(), "/tmp/p30d", 0755) == 0, "mkdir");
	fd = bfree_open(guest_fs(), "/tmp/p30d", 0, 0);
	CHECK(fd >= 0, "opendir");
	rc = bfree_getdents64(guest_fs(), fd, dirbuf, sizeof(dirbuf));
	CHECK(rc > 0, "getdents");
	{
		struct bfree_linux_dirent64 *de =
			(struct bfree_linux_dirent64 *)(void *)dirbuf;

		CHECK(strcmp(de->d_name, ".") == 0, "dot entry");
	}
	bfree_close(guest_fs(), fd);

	/* 4) job-control signals + sigaltstack + TIOCGPGRP write. */
	memset(&act, 0, sizeof(act));
	act.handler = 0x401111UL;
	act.flags = BFREE_SA_RESTORER | BFREE_SA_SIGINFO;
	act.restorer = bfree_signal_restorer_addr();
	CHECK(bfree_invoke_syscall(13, BFREE_SIGTSTP, (unsigned long)&act, 0,
				   sizeof(unsigned long), 0, 0) == 0,
	      "sigaction TSTP");
	stack_top = ((uintptr_t)stackbuf + sizeof(stackbuf)) & ~15ULL;
	self->ring3.valid = 1;
	self->ring3.rsp = stack_top;
	self->ring3.rcx = 0x400333UL;
	CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGTSTP, 0, 0, 0, 0) ==
		      0,
	      "kill TSTP");
	CHECK(self->sig_frame != NULL, "TSTP frame");
	frame = self->sig_frame;
	CHECK(frame->si_signo == BFREE_SIGTSTP, "TSTP signo");
	CHECK(bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0) == 0, "sigreturn");

	sas[0] = 0x7fff0000UL;
	sas[1] = 8192UL;
	sas[2] = 0;
	CHECK(bfree_invoke_syscall(131, (unsigned long)sas, (unsigned long)old_sas,
				   0, 0, 0, 0) == 0,
	      "sigaltstack set");
	CHECK(self->sas_ss_sp == sas[0], "sas sp");
	CHECK(self->sas_ss_size == sas[1], "sas size");

	CHECK(bfree_setsid(guest_proc_mgr()) > 0 ||
		      bfree_getpgid(guest_proc_mgr(), 0) > 0,
	      "sid/pgid");
	pgrp = -1;
	CHECK(bfree_invoke_syscall(16, 0, 0x540F, (unsigned long)&pgrp, 0, 0,
				   0) == 0,
	      "TIOCGPGRP");
	CHECK(pgrp > 0, "pgrp written");

	/* 5) wait4 WNOHANG with no children -> -ECHILD; O_TRUNC open. */
	CHECK(bfree_wait4(guest_proc_mgr(), -1, &status, WNOHANG, NULL) ==
		      -ECHILD,
	      "wait4 no child");
	fd = bfree_open(guest_fs(), "/tmp/p30f", O_TRUNC, 0);
	CHECK(fd >= 0, "open trunc");
	CHECK(bfree_stat(guest_fs(), "/tmp/p30f", &st) == 0, "stat trunc");
	CHECK(st.st_size == 0, "truncated size");
	bfree_close(guest_fs(), fd);

	printf("P30_LINUX_COMPAT: PASS\n");
	return 0;
}
