#ifndef BFREE_SIGNAL_FRAME_H
#define BFREE_SIGNAL_FRAME_H

#include <stddef.h>
#include <stdint.h>

struct bfree_proc_mgr;

#define BFREE_SA_RESTORER 0x04000000UL
#define BFREE_SA_SIGINFO  0x00000004UL
#define BFREE_SIGBIT(sig) (1UL << ((unsigned)(sig) - 1U))

/* Keep in sync with kernel/arch/x86_64/signal_entry.S */
#define BFREE_SF_OFF_UC      8U
#define BFREE_SF_OFF_SIGNUM  232U
#define BFREE_SF_OFF_HANDLER 248U
#define BFREE_SF_SIZE        256U

/* Minimal Linux x86_64 rt_sigframe fields used by L8/L9. */
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
	uint64_t handler; /* L9: trampoline target (not a Linux field) */
};

struct bfree_sigaction_abi {
	unsigned long handler;
	unsigned long flags;
	unsigned long restorer;
	unsigned long mask;
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

/*
 * Deliver one pending signal to the current process:
 * build rt_sigframe on the ring-3 stack (or storage fallback),
 * point RIP at the entry trampoline (loads rdi/rsi/rdx, jmp handler)
 * and pretcode at restorer.
 * Returns delivered signo, 0 if none, or -errno.
 */
int bfree_rt_signal_poll_deliver(struct bfree_proc_mgr *mgr);

uint64_t bfree_signal_restorer_addr(void);
uint64_t bfree_signal_entry_addr(void);

/*
 * Host-visible mirror of the entry trampoline register loads.
 * Writes the values trampoline would place in rdi/rsi/rdx/target.
 */
void bfree_signal_entry_regs(const struct bfree_rt_sigframe *frame,
			     uint64_t *rdi_out, uint64_t *rsi_out,
			     uint64_t *rdx_out, uint64_t *target_out);

#endif
