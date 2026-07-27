/*
 * Return to ring-3 via sysret (M16).
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_ring3_do_sysret
bfree_ring3_do_sysret:
	/* rdi=rax, rsi=rcx, rdx=r11, rcx=rsp */
	mov %rdi, %rax
	mov %rsi, %rcx
	mov %rdx, %r11
	mov %rcx, %rsp
	sysretq
