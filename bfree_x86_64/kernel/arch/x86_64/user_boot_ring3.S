/*
 * Ring-3 entry via iretq (M9).
 * rdi = entry RIP, rsi = user RSP
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_user_boot_exec
bfree_user_boot_exec:
	mov %rdi, %rcx
	mov %rsi, %rdx
	pushq $0x1b
	pushq %rdx
	pushfq
	pop %rax
	or $0x200, %rax
	push %rax
	pushq $0x23
	pushq %rcx
	iretq
