/*
 * P27_SIGNAL_DELIVER — L9 ring-3 signal delivery to handler frame.
 */
#include "process.h"
#include "signal_frame.h"
#include "syscall.h"

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
	struct bfree_sigaction_abi act;
	struct bfree_proc *self;
	struct bfree_rt_sigframe *frame;
	unsigned char stackbuf[4096];
	uintptr_t stack_top;
	unsigned long blockset;
	unsigned long old = 0;
	long rc;

	guest_init();
	self = bfree_proc_current(guest_proc_mgr());
	CHECK(self != NULL, "current proc");

	memset(&act, 0, sizeof(act));
	act.handler = 0x401234UL;
	act.flags = BFREE_SA_RESTORER;
	act.restorer = bfree_signal_restorer_addr();
	act.mask = 0x4UL; /* also block bit2 while handling */

	CHECK(bfree_invoke_syscall(13, BFREE_SIGINT, (unsigned long)&act, 0,
				   sizeof(unsigned long), 0, 0) == 0,
	      "install SIGINT handler");

	stack_top = ((uintptr_t)stackbuf + sizeof(stackbuf)) & ~15ULL;
	self->ring3.valid = 1;
	self->ring3.rsp = stack_top;
	self->ring3.rcx = 0x400100UL; /* interrupted RIP */
	self->ring3.rax = 0x99UL;
	self->ring3.r11 = 0x202UL;

	CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGINT, 0, 0, 0, 0) == 0,
	      "kill self SIGINT");

	CHECK(self->sig_frame != NULL, "frame attached after kill");
	frame = self->sig_frame;
	CHECK(frame->si_signo == BFREE_SIGINT, "frame signo");
	CHECK(frame->pretcode == act.restorer, "pretcode restorer");
	CHECK(frame->handler == act.handler, "frame handler");
	CHECK(frame->uc.uc_mcontext.rip == 0x400100UL, "saved rip");
	CHECK(frame->uc.uc_mcontext.rax == 0x99UL, "saved rax");
	CHECK(self->ring3.rcx == bfree_signal_entry_addr(),
	      "RIP redirected to entry trampoline");
	CHECK(self->ring3.rsp == (uint64_t)(uintptr_t)frame, "RSP at frame");
	CHECK((self->sig_mask & BFREE_SIGBIT(BFREE_SIGINT)) != 0,
	      "SIGINT blocked in handler");
	CHECK((self->sig_mask & act.mask) == act.mask, "sa_mask applied");

	rc = bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0);
	CHECK(rc == 0x99, "sigreturn restores rax");
	CHECK(self->sig_frame == NULL, "frame cleared");
	CHECK(self->ring3.rcx == 0x400100UL, "RIP restored");
	CHECK(self->ring3.rsp == stack_top, "RSP restored");
	CHECK((self->sig_mask & BFREE_SIGBIT(BFREE_SIGINT)) == 0,
	      "SIGINT unblocked after return");

	/* Blocked signal is not delivered. */
	blockset = BFREE_SIGBIT(BFREE_SIGINT);
	CHECK(bfree_invoke_syscall(14, 0 /* BLOCK */, (unsigned long)&blockset,
				   (unsigned long)&old, sizeof(unsigned long), 0,
				   0) == 0,
	      "block SIGINT");
	self->ring3.rcx = 0x400200UL;
	self->ring3.rsp = stack_top;
	CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGINT, 0, 0, 0, 0) == 0,
	      "kill while blocked");
	CHECK(self->sig_frame == NULL, "no deliver while blocked");
	CHECK(self->ring3.rcx == 0x400200UL, "RIP unchanged while blocked");

	printf("P27_SIGNAL_DELIVER: PASS\n");
	return 0;
}
