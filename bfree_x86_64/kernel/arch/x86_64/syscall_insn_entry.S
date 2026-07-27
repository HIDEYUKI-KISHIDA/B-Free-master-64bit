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
	mov user_rsp_save(%rip), %rdi
	mov %rcx, %rsi
	mov %r11, %rdx
	call bfree_ring3_syscall_pre
	mov %r8, %r9
	mov %r10, %r8
	mov %rdx, %rcx
	mov %rsi, %rdx
	mov %rdi, %rsi
	mov %rax, %rdi
	call bfree_invoke_syscall
	call bfree_syscall_exec_resume_if_needed
	mov %rax, %rdi
	call bfree_ring3_syscall_post
	mov ring3_sysret_rcx(%rip), %rcx
	mov ring3_sysret_r11(%rip), %r11
	mov ring3_sysret_rsp(%rip), %rsp
	mov ring3_sysret_rax(%rip), %rax
	sysretq
