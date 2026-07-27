#include "signal_frame.h"

#include <errno.h>
#include <string.h>

int bfree_rt_sigframe_setup(struct bfree_proc_mgr *mgr,
			    struct bfree_rt_sigframe *frame, int sig,
			    uint64_t restorer, uint64_t rip, uint64_t rsp,
			    uint64_t rax, unsigned long old_mask)
{
	struct bfree_proc *self;

	if (mgr == NULL || frame == NULL)
		return -EFAULT;
	if (sig <= 0 || sig >= 64)
		return -EINVAL;
	self = bfree_proc_current(mgr);
	if (self == NULL)
		return -ESRCH;

	memset(frame, 0, sizeof(*frame));
	frame->pretcode = restorer;
	frame->uc.uc_flags = 0;
	frame->uc.uc_link = 0;
	frame->uc.uc_stack_sp = rsp;
	frame->uc.uc_stack_flags = 2; /* SS_DISABLE */
	frame->uc.uc_stack_size = 0;
	frame->uc.uc_mcontext.rip = rip;
	frame->uc.uc_mcontext.rsp = rsp;
	frame->uc.uc_mcontext.rax = rax;
	frame->uc.uc_mcontext.rcx = rip; /* SYSCALL return RIP shadow */
	frame->uc.uc_mcontext.r11 = 0x202; /* IF set */
	frame->uc.uc_mcontext.eflags = 0x202;
	frame->uc.uc_mcontext.cs = 0x33;
	frame->uc.uc_mcontext.ss = 0x2b;
	frame->uc.uc_mcontext.oldmask = old_mask;
	frame->uc.uc_sigmask = old_mask;
	frame->si_signo = sig;
	frame->si_errno = 0;
	frame->si_code = 0;

	self->sig_frame = frame;
	return 0;
}

long bfree_rt_sigreturn(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *self;
	struct bfree_rt_sigframe *frame;
	uint64_t rax;

	if (mgr == NULL)
		return -EFAULT;
	self = bfree_proc_current(mgr);
	if (self == NULL)
		return -ESRCH;
	frame = self->sig_frame;
	if (frame == NULL)
		return -EFAULT;

	self->sig_mask = frame->uc.uc_sigmask;
	rax = frame->uc.uc_mcontext.rax;

	if (self->ring3.valid) {
		self->ring3.rcx = frame->uc.uc_mcontext.rip;
		self->ring3.rsp = frame->uc.uc_mcontext.rsp;
		self->ring3.r11 = frame->uc.uc_mcontext.r11 != 0
					  ? frame->uc.uc_mcontext.r11
					  : 0x202;
		self->ring3.rax = rax;
	}

	self->sig_frame = NULL;
	return (long)rax;
}
