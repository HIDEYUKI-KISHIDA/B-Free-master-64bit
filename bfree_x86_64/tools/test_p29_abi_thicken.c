/*
 * P29_ABI_THICKEN — signal siginfo + waitid + poll/access/umask + no ash trampoline.
 */
#include "fs_ofd.h"
#include "process.h"
#include "signal_frame.h"
#include "syscall.h"
#include "syscall_dispatch.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

static int file_has_sym(const char *path, const char *sym, int expect_present)
{
	char cmd[512];
	int rc;

	snprintf(cmd, sizeof(cmd),
		 "nm -g --defined-only %s 2>/dev/null | grep -F -q %s", path,
		 sym);
	rc = system(cmd);
	if (expect_present)
		return rc == 0;
	return rc != 0;
}

int main(void)
{
	struct bfree_sigaction_abi act;
	struct bfree_proc *self;
	struct bfree_rt_sigframe *frame;
	struct bfree_pollfd pfd;
	struct bfree_linux_stat st;
	unsigned char stackbuf[4096];
	unsigned char termios_buf[64];
	unsigned char siginfo[128];
	unsigned long pending = 0;
	uintptr_t stack_top;
	int pipefd[2];
	int child;
	long rc;
	unsigned old_umask;

	guest_init();
	self = bfree_proc_current(guest_proc_mgr());
	CHECK(self != NULL, "current");

	/* 1) Signal fidelity: si_code/si_pid on kill delivery. */
	memset(&act, 0, sizeof(act));
	act.handler = 0x401000UL;
	act.flags = BFREE_SA_RESTORER | BFREE_SA_SIGINFO;
	act.restorer = bfree_signal_restorer_addr();
	CHECK(bfree_invoke_syscall(13, BFREE_SIGINT, (unsigned long)&act, 0,
				   sizeof(unsigned long), 0, 0) == 0,
	      "sigaction");
	stack_top = ((uintptr_t)stackbuf + sizeof(stackbuf)) & ~15ULL;
	self->ring3.valid = 1;
	self->ring3.rsp = stack_top;
	self->ring3.rcx = 0x400100UL;
	CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGINT, 0, 0, 0, 0) == 0,
	      "kill");
	CHECK(self->sig_frame != NULL, "frame");
	frame = self->sig_frame;
	CHECK(frame->si_signo == BFREE_SIGINT, "si_signo");
	CHECK(frame->si_code == BFREE_SI_USER, "si_code SI_USER");
	CHECK(frame->si_pid == self->pid, "si_pid sender");
	CHECK(bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0) == 0, "sigreturn");

	/* Block then kill — pending bit must show; no frame while blocked. */
	{
		unsigned long block = BFREE_SIGBIT(BFREE_SIGINT);
		unsigned long old = 0;

		CHECK(bfree_invoke_syscall(14, 0, (unsigned long)&block,
					   (unsigned long)&old,
					   sizeof(unsigned long), 0, 0) == 0,
		      "block SIGINT");
		self->ring3.rcx = 0x400200UL;
		self->ring3.rsp = stack_top;
		CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGINT, 0, 0, 0,
					   0) == 0,
		      "kill blocked");
		CHECK(self->sig_frame == NULL, "no deliver while blocked");
		CHECK(bfree_invoke_syscall(127, (unsigned long)&pending, 8, 0, 0,
					   0, 0) == 0,
		      "rt_sigpending blocked");
		CHECK((pending & BFREE_SIGBIT(BFREE_SIGINT)) != 0,
		      "SIGINT pending bit");
		block = 0;
		CHECK(bfree_invoke_syscall(14, 2, (unsigned long)&block,
					   (unsigned long)&old,
					   sizeof(unsigned long), 0, 0) == 0,
		      "unblock");
		(void)bfree_rt_signal_poll_deliver(guest_proc_mgr());
		CHECK(self->sig_frame != NULL, "deliver after unblock");
		CHECK(bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0) == 0,
		      "sigreturn2");
	}

	/* 2) waitid fills Linux siginfo; returns 0. */
	child = bfree_fork(guest_proc_mgr());
	CHECK(child > 0, "fork");
	CHECK(bfree_switch_proc(guest_proc_mgr(), child) == 0, "switch child");
	bfree_exit(guest_proc_mgr(), 44);
	CHECK(bfree_switch_proc(guest_proc_mgr(), self->pid) == 0,
	      "switch parent");
	memset(siginfo, 0xa5, sizeof(siginfo));
	rc = bfree_invoke_syscall(247, P_PID, child, (unsigned long)siginfo,
				  WEXITED, 0, 0);
	CHECK(rc == 0, "waitid syscall returns 0");
	CHECK(*(int *)(siginfo + 0) == BFREE_SIGCHLD, "waitid si_signo");
	CHECK(*(int *)(siginfo + 8) == BFREE_CLD_EXITED, "waitid si_code");
	CHECK(*(int *)(siginfo + 16) == child, "waitid si_pid");
	CHECK(*(int *)(siginfo + 24) == 44, "waitid si_status");

	/* 3) poll: nfds==0 and POLLHUP. */
	CHECK(bfree_poll(guest_proc_mgr(), guest_fs(), NULL, 0, 0) == 0,
	      "poll nfds0");
	CHECK(bfree_pipe_open(guest_proc_mgr(), pipefd) == 0, "pipe");
	bfree_pipe_close(guest_proc_mgr(), pipefd[1]);
	pfd.fd = pipefd[0];
	pfd.events = BFREE_POLLIN;
	pfd.revents = 0;
	CHECK(bfree_poll(guest_proc_mgr(), guest_fs(), &pfd, 1, 0) == 1,
	      "poll hup ready");
	CHECK((pfd.revents & BFREE_POLLHUP) != 0, "POLLHUP");
	bfree_pipe_close(guest_proc_mgr(), pipefd[0]);

	/* 4) access mode + umask create. */
	old_umask = bfree_umask(guest_fs(), 0077);
	CHECK(bfree_create(guest_fs(), "/tmp/p29_mode", 0777) == 0, "create");
	CHECK(bfree_stat(guest_fs(), "/tmp/p29_mode", &st) == 0, "stat");
	CHECK((st.st_mode & 0777) == 0700, "umask applied");
	CHECK(bfree_access(guest_fs(), "/tmp/p29_mode", R_OK | W_OK | X_OK) == 0,
	      "access rwx");
	CHECK(bfree_chmod(guest_fs(), "/tmp/p29_mode", 0) == 0, "chmod 0");
	/* uid 0 still may R/W; X needs an x bit — expect -EACCES for X_OK. */
	CHECK(bfree_access(guest_fs(), "/tmp/p29_mode", X_OK) == -EACCES,
	      "access X denied");
	(void)bfree_umask(guest_fs(), old_umask);

	/* 5) TCGETS fills termios. */
	memset(termios_buf, 0, sizeof(termios_buf));
	CHECK(bfree_invoke_syscall(16, 0, 0x5401, (unsigned long)termios_buf, 0,
				   0, 0) == 0,
	      "TCGETS");
	CHECK(termios_buf[0] != 0 || termios_buf[4] != 0 || termios_buf[8] != 0,
	      "termios non-zero");

	/* 6) QEMU ash proof: trampolines unlinked; Linux stack symbols present. */
	CHECK(access("build/kernel.elf", R_OK) == 0, "kernel.elf available");
	CHECK(file_has_sym("build/kernel.elf", "ash_guest_tramp", 0) == 1,
	      "no ash_guest_tramp");
	CHECK(file_has_sym("build/kernel.elf", "ash_regress_tramp", 0) == 1,
	      "no ash_regress_tramp");
	CHECK(file_has_sym("build/kernel.elf", "guest_exec_tramp", 0) == 1,
	      "no guest_exec_tramp");
	CHECK(file_has_sym("build/kernel.elf", "bfree_linux_user_stack_build",
			   1) == 1,
	      "linux stack builder linked");
	CHECK(file_has_sym("build/kernel.elf", "bfree_ash_guest_boot", 1) == 1,
	      "ash guest boot linked");

	printf("P29_ABI_THICKEN: PASS\n");
	return 0;
}
