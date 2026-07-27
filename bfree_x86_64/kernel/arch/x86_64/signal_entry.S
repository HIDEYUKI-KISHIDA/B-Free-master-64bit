/*
 * Signal entry trampoline (L10).
 *
 * On entry RSP points at struct bfree_rt_sigframe.
 * Loads Linux sa_sigaction argument registers, then jumps to frame->handler:
 *   rdi = signo
 *   rsi = &siginfo (si_signo)
 *   rdx = &ucontext
 *
 * Offsets must match signal_frame.h (checked by _Static_assert in C).
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_signal_entry
	.globl bfree_signal_entry_end

	/* offsetof(struct bfree_rt_sigframe, ...) */
	.set SF_UC, 8
	.set SF_SIGNUM, 232
	.set SF_HANDLER, 248

bfree_signal_entry:
	movl SF_SIGNUM(%rsp), %edi
	leaq SF_SIGNUM(%rsp), %rsi
	leaq SF_UC(%rsp), %rdx
	movq SF_HANDLER(%rsp), %rax
	jmpq *%rax
bfree_signal_entry_end:
