/*
 * x86_64 SYSCALL instruction entry (M9) with kernel stack switch.
 */
	.section .note.GNU-stack,"",@progbits
	.text
	.globl bfree_syscall_insn_entry

	.section .bss
	.align 8
user_rsp_save:
	.space 8
	.align 16
kernel_syscall_stack:
	.space 16384
kernel_syscall_stack_top:

	.text
bfree_syscall_insn_entry:
	mov %rsp, user_rsp_save(%rip)
	mov $kernel_syscall_stack_top, %rsp
	push %r11
	push %rcx
	push %r9
	mov %r8, %r9
	mov %r10, %r8
	mov %rdx, %rcx
	mov %rsi, %rdx
	mov %rdi, %rsi
	mov %rax, %rdi
	call bfree_invoke_syscall
	add $8, %rsp
	pop %rcx
	pop %r11
	mov user_rsp_save(%rip), %rsp
	sysretq
