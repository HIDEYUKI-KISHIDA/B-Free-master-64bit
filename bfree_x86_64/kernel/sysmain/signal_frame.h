#ifndef BFREE_SIGNAL_FRAME_H
#define BFREE_SIGNAL_FRAME_H

#include "process.h"

#include <stddef.h>
#include <stdint.h>

/* Minimal Linux x86_64 rt_sigframe fields used by L8 restore. */
struct bfree_sigcontext {
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rbp;
	uint64_t rbx;
	uint64_t rdx;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rsp;
	uint64_t rip;
	uint64_t eflags;
	uint16_t cs;
	uint16_t gs;
	uint16_t fs;
	uint16_t ss;
	uint64_t err;
	uint64_t trapno;
	uint64_t oldmask;
	uint64_t cr2;
};

struct bfree_ucontext {
	uint64_t uc_flags;
	uint64_t uc_link;
	uint64_t uc_stack_sp;
	int32_t uc_stack_flags;
	uint32_t uc_stack_size;
	struct bfree_sigcontext uc_mcontext;
	uint64_t uc_sigmask;
};

struct bfree_rt_sigframe {
	uint64_t pretcode;
	struct bfree_ucontext uc;
	int32_t si_signo;
	int32_t si_errno;
	int32_t si_code;
	uint32_t pad;
};

/*
 * Fill frame and attach it to current process for a later rt_sigreturn.
 * restorer becomes pretcode (handler return path).
 */
int bfree_rt_sigframe_setup(struct bfree_proc_mgr *mgr,
			    struct bfree_rt_sigframe *frame, int sig,
			    uint64_t restorer, uint64_t rip, uint64_t rsp,
			    uint64_t rax, unsigned long old_mask);

/* Restore uc_sigmask (+ ring3 RIP/RSP/RAX when present). */
long bfree_rt_sigreturn(struct bfree_proc_mgr *mgr);

#endif
