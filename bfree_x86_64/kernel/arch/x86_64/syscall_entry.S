/*
 * x86_64 Linux syscall trap entry (M6).
 * On entry: rax=nr, rdi=a0, rsi=a1, rdx=a2, r10=a3, r8=a4, r9=a5.
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_trap_syscall_entry
bfree_trap_syscall_entry:
	push %rbp
	mov %rsp, %rbp
	push %r9
	mov %r8, %r9
	mov %r10, %r8
	mov %rdx, %rcx
	mov %rsi, %rdx
	mov %rdi, %rsi
	mov %rax, %rdi
	push 16(%rbp)
	call bfree_invoke_syscall
	add $8, %rsp
	pop %r9
	pop %rbp
	ret
