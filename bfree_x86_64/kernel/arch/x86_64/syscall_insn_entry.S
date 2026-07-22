/*
 * x86_64 SYSCALL instruction entry (M9).
 * On entry: rax=nr, rdi..r9 per Linux ABI; rcx=return RIP, r11=return RFLAGS.
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_syscall_insn_entry
bfree_syscall_insn_entry:
	push %rbp
	mov %rsp, %rbp
	push %rcx
	push %r11
	mov %r9, %r9
	mov %r8, %r8
	mov %r10, %r10
	mov %rdx, %rcx
	mov %rsi, %rdx
	mov %rdi, %rsi
	mov %rax, %rdi
	push 16(%rbp)
	call bfree_invoke_syscall
	add $8, %rsp
	pop %r11
	pop %rcx
	pop %rbp
	sysretq
