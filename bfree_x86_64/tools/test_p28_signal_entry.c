/*
 * P28_SIGNAL_ENTRY — L10 entry trampoline (rdi/rsi/rdx before handler).
 */
#include "process.h"
#include "signal_frame.h"
#include "syscall.h"

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
	uint64_t rdi = 0, rsi = 0, rdx = 0, target = 0;
	uintptr_t base;

	guest_init();
	self = bfree_proc_current(guest_proc_mgr());
	CHECK(self != NULL, "current proc");

	memset(&act, 0, sizeof(act));
	act.handler = 0x401234UL;
	act.flags = BFREE_SA_RESTORER | BFREE_SA_SIGINFO;
	act.restorer = bfree_signal_restorer_addr();
	act.mask = 0;

	CHECK(bfree_invoke_syscall(13, BFREE_SIGINT, (unsigned long)&act, 0,
				   sizeof(unsigned long), 0, 0) == 0,
	      "install SIGINT SA_SIGINFO handler");

	stack_top = ((uintptr_t)stackbuf + sizeof(stackbuf)) & ~15ULL;
	self->ring3.valid = 1;
	self->ring3.rsp = stack_top;
	self->ring3.rcx = 0x400100UL;
	self->ring3.rax = 0x99UL;
	self->ring3.r11 = 0x202UL;

	CHECK(bfree_invoke_syscall(62, self->pid, BFREE_SIGINT, 0, 0, 0, 0) == 0,
	      "kill self SIGINT");

	CHECK(self->sig_frame != NULL, "frame attached");
	frame = self->sig_frame;
	base = (uintptr_t)frame;

	CHECK(self->ring3.rcx == bfree_signal_entry_addr(),
	      "RIP redirected to entry trampoline");
	CHECK(self->ring3.rsp == (uint64_t)base, "RSP at frame");
	CHECK(frame->handler == act.handler, "frame handler target");
	CHECK(frame->si_signo == BFREE_SIGINT, "si_signo");
	CHECK(frame->pretcode == act.restorer, "pretcode restorer");

	bfree_signal_entry_regs(frame, &rdi, &rsi, &rdx, &target);
	CHECK(rdi == (uint64_t)BFREE_SIGINT, "entry rdi = signo");
	CHECK(rsi == (uint64_t)(base + BFREE_SF_OFF_SIGNUM), "entry rsi = &siginfo");
	CHECK(rdx == (uint64_t)(base + BFREE_SF_OFF_UC), "entry rdx = &ucontext");
	CHECK(target == act.handler, "entry jmp target = handler");

	/* Saved mcontext also mirrors the SA_SIGINFO arg layout. */
	CHECK(frame->uc.uc_mcontext.rdi == (uint64_t)BFREE_SIGINT, "mcontext rdi");
	CHECK(frame->uc.uc_mcontext.rsi == rsi, "mcontext rsi");
	CHECK(frame->uc.uc_mcontext.rdx == rdx, "mcontext rdx");

	CHECK(bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0) == 0x99,
	      "sigreturn after entry path");
	CHECK(self->sig_frame == NULL, "frame cleared");
	CHECK(self->ring3.rcx == 0x400100UL, "RIP restored");

	printf("P28_SIGNAL_ENTRY: PASS\n");
	return 0;
}
