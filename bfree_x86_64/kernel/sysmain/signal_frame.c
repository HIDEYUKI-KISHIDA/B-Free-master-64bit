#include "signal_frame.h"
#include "process.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

extern void bfree_signal_restorer(void);

uint64_t bfree_signal_restorer_addr(void)
{
	return (uint64_t)(uintptr_t)bfree_signal_restorer;
}

static int frame_space_ok(void)
{
	return sizeof(struct bfree_rt_sigframe) <= BFREE_RT_SIGFRAME_SPACE;
}

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
	if (!frame_space_ok())
		return -ENOMEM;

	memset(frame, 0, sizeof(*frame));
	frame->pretcode = restorer != 0 ? restorer : bfree_signal_restorer_addr();
	frame->uc.uc_flags = 0;
	frame->uc.uc_link = 0;
	frame->uc.uc_stack_sp = rsp;
	frame->uc.uc_stack_flags = 2; /* SS_DISABLE */
	frame->uc.uc_stack_size = 0;
	frame->uc.uc_mcontext.rip = rip;
	frame->uc.uc_mcontext.rsp = rsp;
	frame->uc.uc_mcontext.rax = rax;
	frame->uc.uc_mcontext.rdi = (uint64_t)(unsigned)sig;
	frame->uc.uc_mcontext.rcx = rip;
	frame->uc.uc_mcontext.r11 = 0x202;
	frame->uc.uc_mcontext.eflags = 0x202;
	frame->uc.uc_mcontext.cs = 0x33;
	frame->uc.uc_mcontext.ss = 0x2b;
	frame->uc.uc_mcontext.oldmask = old_mask;
	frame->uc.uc_sigmask = old_mask;
	frame->si_signo = sig;
	frame->si_errno = 0;
	frame->si_code = 0;
	frame->handler = 0;

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

static int has_pending(const struct bfree_proc *self, int sig)
{
	if (sig == BFREE_SIGINT)
		return self->sigint_pending != 0;
	if (sig == BFREE_SIGPIPE)
		return self->sigpipe_pending != 0;
	if (sig == BFREE_SIGCHLD)
		return self->sigchld_pending != 0;
	return 0;
}

static void clear_pending(struct bfree_proc *self, int sig)
{
	if (sig == BFREE_SIGINT)
		self->sigint_pending = 0;
	else if (sig == BFREE_SIGPIPE)
		self->sigpipe_pending = 0;
	else if (sig == BFREE_SIGCHLD)
		self->sigchld_pending = 0;
}

static int deliver_one(struct bfree_proc_mgr *mgr, struct bfree_proc *self,
		       int sig)
{
	struct bfree_rt_sigframe *frame;
	uint64_t old_rsp;
	uint64_t old_rip;
	uint64_t old_rax;
	uint64_t restorer;
	unsigned long old_mask;
	uintptr_t frame_addr;

	if (sig <= 0 || sig >= 64)
		return -EINVAL;
	if (self->sig_frame != NULL)
		return 0; /* already in a handler frame */
	if (!frame_space_ok())
		return -ENOMEM;

	old_mask = self->sig_mask;
	old_rsp = self->ring3.valid ? self->ring3.rsp : 0;
	old_rip = self->ring3.valid ? self->ring3.rcx : 0;
	old_rax = self->ring3.valid ? self->ring3.rax : 0;

	if (self->ring3.valid && old_rsp > sizeof(*frame) + 32U) {
		frame_addr = (uintptr_t)((old_rsp - sizeof(*frame)) & ~15ULL);
		frame = (struct bfree_rt_sigframe *)frame_addr;
	} else {
		frame = (struct bfree_rt_sigframe *)(void *)self->sig_frame_storage;
		frame_addr = (uintptr_t)frame;
	}

	restorer = self->sig_restorer[sig];
	if (restorer == 0)
		restorer = bfree_signal_restorer_addr();

	if (bfree_rt_sigframe_setup(mgr, frame, sig, restorer, old_rip, old_rsp,
				    old_rax, old_mask) != 0)
		return -EFAULT;
	frame->handler = self->sig_handler[sig];
	frame->uc.uc_mcontext.rsi = frame_addr +
				    offsetof(struct bfree_rt_sigframe, si_signo);
	frame->uc.uc_mcontext.rdx = frame_addr +
				    offsetof(struct bfree_rt_sigframe, uc);

	/* Block this signal + sa_mask while handler runs. */
	self->sig_mask |= BFREE_SIGBIT(sig) | self->sig_sa_mask[sig];

	self->ring3.rcx = self->sig_handler[sig];
	self->ring3.rsp = frame_addr;
	self->ring3.r11 = 0x202;
	self->ring3.rax = 0;
	self->ring3.valid = 1;
	return sig;
}

int bfree_rt_signal_poll_deliver(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *self;
	static const int order[] = { BFREE_SIGINT, BFREE_SIGPIPE, BFREE_SIGCHLD };
	size_t i;

	if (mgr == NULL)
		return -EFAULT;
	self = bfree_proc_current(mgr);
	if (self == NULL)
		return -ESRCH;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		int sig = order[i];
		unsigned long handler;

		if (!has_pending(self, sig))
			continue;
		handler = self->sig_handler[sig];
		if (handler == 1 /* SIG_IGN */) {
			clear_pending(self, sig);
			continue;
		}
		if (handler == 0 /* SIG_DFL */) {
			clear_pending(self, sig);
			continue;
		}
		if (self->sig_mask & BFREE_SIGBIT(sig))
			continue; /* keep pending while blocked */
		clear_pending(self, sig);
		return deliver_one(mgr, self, sig);
	}
	return 0;
}
