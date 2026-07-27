/*
 * Default SA_RESTORER: invoke rt_sigreturn (NR 15).
 * Used as pretcode when userland does not provide a restorer.
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_signal_restorer
	.globl bfree_signal_restorer_end
bfree_signal_restorer:
	mov $15, %eax
	syscall
	/* If sigreturn returns (error), hang politely. */
1:
	jmp 1b
bfree_signal_restorer_end:
