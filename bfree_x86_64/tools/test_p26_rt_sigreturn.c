/*
 * P26_RT_SIGRETURN — L8 Linux rt_sigframe restore.
 */
#include "process.h"
#include "signal_frame.h"
#include "syscall.h"
#include "syscall_dispatch.h"

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
	struct bfree_rt_sigframe frame;
	struct bfree_proc *self;
	unsigned long old = 0;
	unsigned long mask = 0x55AAUL;
	long rc;

	guest_init();
	CHECK(bfree_syscall_is_implemented(15), "rt_sigreturn registered");

	/* No active frame => honest failure */
	rc = bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0);
	CHECK(rc == -EFAULT, "rt_sigreturn without frame");

	self = bfree_proc_current(guest_proc_mgr());
	CHECK(self != NULL, "current proc");

	/* Install a blocked mask, then prepare a frame that restores another. */
	CHECK(bfree_invoke_syscall(14, 2, (unsigned long)&mask,
				   (unsigned long)&old, sizeof(unsigned long), 0,
				   0) == 0,
	      "setmask");
	CHECK(bfree_rt_sigframe_setup(guest_proc_mgr(), &frame, 2 /* SIGINT */,
				      0x401000UL, 0x402000UL, 0x7fff0000UL,
				      0x11UL, 0xA5A5UL) == 0,
	      "sigframe setup");
	CHECK(self->sig_frame == &frame, "frame attached");
	CHECK(frame.uc.uc_sigmask == 0xA5A5UL, "frame mask");
	CHECK(frame.uc.uc_mcontext.rip == 0x402000UL, "frame rip");
	CHECK(frame.si_signo == 2, "frame signo");

	rc = bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0);
	CHECK(rc == 0x11, "rt_sigreturn restores rax");
	CHECK(self->sig_frame == NULL, "frame consumed");
	CHECK(self->sig_mask == 0xA5A5UL, "sigmask restored");

	/* Second call without new frame fails honestly again. */
	rc = bfree_invoke_syscall(15, 0, 0, 0, 0, 0, 0);
	CHECK(rc == -EFAULT, "second rt_sigreturn without frame");

	printf("P26_RT_SIGRETURN: PASS\n");
	return 0;
}
