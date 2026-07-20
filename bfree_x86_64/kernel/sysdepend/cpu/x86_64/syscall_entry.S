# セキュリティ警告抑制用ディレクティブ

.section .note.GNU-stack,"",@progbits

#-------------------------------------------------------------

# syscall_entry.S - x86_64 SYSCALL/SYSRETエントリ雛形

# 仕様: TK2_x86_64_Spec.md v2.4準拠

#

# SYSCALL does not switch RSP to TSS.rsp0 (unlike interrupt gates).

# Run the C handler on a dedicated kernel stack so ring3 RSP / red zone

# are not clobbered (Qt guest + deep init after exec_initrd).

#

# Linux preserves RSI (and other arg regs) across syscalls. musl readdir

# keeps RSI = dir->buf across getdents64; restore arg regs the C handler clobbers.

#-------------------------------------------------------------

	.section .bss

	.align 16

	.lcomm bfree_syscall_kstack, 16384

	.set bfree_syscall_kstack_top, bfree_syscall_kstack + 16384



	.section .text

	.global syscall_entry

	.type syscall_entry, @function



	.extern g_bfree_sysret_exec_rsp

	.extern g_bfree_sysret_exec_rcx

	.extern g_bfree_sysret_exec_r11

	.extern g_bfree_sysret_exec_rdi

	.extern g_bfree_sysret_exec_rsi

	.extern g_bfree_sysret_exec_rdx

	.extern g_bfree_exec_transfer_rip

	.extern g_bfree_sysret_sig_rax

	.extern g_bfree_user_sysret_rcx

	.extern g_bfree_user_sysret_r11

	.extern g_bfree_user_sysret_rsp

	.extern g_bfree_user_sysret_rbx

	.extern g_bfree_user_sysret_rbp

	.extern g_bfree_user_sysret_r12

	.extern g_bfree_user_sysret_r13

	.extern g_bfree_user_sysret_r14

	.extern g_bfree_user_sysret_r15

	.extern g_bfree_user_sysret_rdx

	.extern g_bfree_fork_parent_ret

	.extern g_bfree_fork_saved_rbx

	.extern g_bfree_fork_saved_rbp

	.extern g_bfree_fork_saved_r12

	.extern g_bfree_fork_saved_r13

	.extern g_bfree_fork_saved_r14

	.extern g_bfree_fork_saved_r15

	.extern g_bfree_fork_saved_rdx

	.extern g_bfree_fork_saved_rcx

	.extern g_bfree_fork_saved_r11

	.extern g_bfree_fork_saved_rsp

	.extern g_bfree_sysret_exec_cr3



# MSR定義

.set MSR_STAR,      0xC0000081

.set MSR_LSTAR,     0xC0000082

.set MSR_SFMASK,    0xC0000084

.set MSR_EFER,      0xC0000080



# SYSCALL/SYSRET用エントリ

syscall_entry:

	# x86_64 syscall ABI:

	#   RAX=syscall番号, RDI,RSI,RDX,R10,R8,R9=引数

	#   RCX=user RIP, R11=user RFLAGS, RSP=user stack

	movq %rcx, g_bfree_user_sysret_rcx(%rip)

	movq %r11, g_bfree_user_sysret_r11(%rip)

	movq %rsp, g_bfree_user_sysret_rsp(%rip)

	/* Callee-saved: needed so cooperative vfork can restore the parent. */
	movq %rbx, g_bfree_user_sysret_rbx(%rip)

	movq %rbp, g_bfree_user_sysret_rbp(%rip)

	movq %r12, g_bfree_user_sysret_r12(%rip)

	movq %r13, g_bfree_user_sysret_r13(%rip)

	movq %r14, g_bfree_user_sysret_r14(%rip)

	movq %r15, g_bfree_user_sysret_r15(%rip)

	movq %rdx, g_bfree_user_sysret_rdx(%rip)



	lea bfree_syscall_kstack_top(%rip), %rsp

	sub $128, %rsp



	push %r10

	push %r9

	push %r8

	push %rdx

	push %rsi

	push %rdi

	# Preserve the user's callee-saved R15.  Do not use R15 as a temporary
	# for the user RSP: libc keeps live values there across syscall(), and
	# replacing it with the stack pointer corrupted BusyBox tar's byte count.
	push %r15

	push %r11

	push %rcx



	# C ABIへ num,arg1,arg2,arg3,arg4,arg5 の順へ並べ替える。

	mov %r8, %r9

	mov %r10, %r8

	mov %rdx, %rcx

	mov %rsi, %rdx

	mov %rdi, %rsi

	mov %rax, %rdi

	call knl_syscall_handler



	# exec_initrd: magic return -4094 => set user RIP/RSP/RFLAGS then SYSRET

	cmpq $-4094, %rax

	je .bfree_syscall_exec_transfer

	# cooperative fork: magic return -4093 => parent resumes after child exit

	cmpq $-4093, %rax

	je .bfree_syscall_fork_parent

	cmpq $-4092, %rax

	je .bfree_syscall_fork_parent

	cmpq $-4091, %rax

	je .bfree_syscall_signal_deliver

	cmpq $-4090, %rax

	je .bfree_syscall_thread_child



.bfree_syscall_normal_return:

	pop %rcx

	pop %r11

	pop %r15

	pop %rdi

	pop %rsi

	pop %rdx

	pop %r8

	pop %r9

	pop %r10

	movq g_bfree_user_sysret_rsp(%rip), %rsp

	sysretq



.bfree_syscall_fork_parent:

	pop %rcx

	pop %r11

	pop %r15

	pop %rdi

	pop %rsi

	pop %rdx

	pop %r8

	pop %r9

	pop %r10

	/* Restore parent callee-saved regs frozen at vfork (child clobbered them).
	 * Load RAX/RCX/R11 before switching to the user RSP: an IRQ after
	 * `mov user_rsp,%rsp` while still CPL0 would push onto the user stack and
	 * can leave RAX=0 (AS-copy fork parent got pid=0 → NULL PF in syscall()). */
	movq g_bfree_fork_saved_rbx(%rip), %rbx

	movq g_bfree_fork_saved_rbp(%rip), %rbp

	movq g_bfree_fork_saved_r12(%rip), %r12

	movq g_bfree_fork_saved_r13(%rip), %r13

	movq g_bfree_fork_saved_r14(%rip), %r14

	movq g_bfree_fork_saved_r15(%rip), %r15

	movq g_bfree_fork_saved_rdx(%rip), %rdx

	movq g_bfree_fork_saved_rcx(%rip), %rcx

	movq g_bfree_fork_saved_r11(%rip), %r11

	movq g_bfree_fork_parent_ret(%rip), %rax

	/* CR3 is switched in C (bfree_coop_as_switch_to / exit_restore).
	 * Do NOT consume g_bfree_sysret_exec_cr3 here — that belongs to
	 * EXEC_TRANSFER only; a leftover child_pt poisoned parent resume
	 * (late-suite cat|grep RIP=0 / RSP=parent). */
.bfree_syscall_fork_parent_rsp:

	movq g_bfree_fork_saved_rsp(%rip), %rsp

	sysretq



.bfree_syscall_exec_transfer:

	pop %rcx

	pop %r11

	pop %r15

	pop %rdi

	pop %rsi

	pop %rdx

	pop %r8

	pop %r9

	pop %r10

	/* Optional private-AS switch before installing the new user RSP. */
	movq g_bfree_sysret_exec_cr3(%rip), %rax
	testq %rax, %rax
	jz .bfree_syscall_exec_transfer_rsp
	movq %rax, %cr3
.bfree_syscall_exec_transfer_rsp:

	movq g_bfree_sysret_exec_rsp(%rip), %rsp

	movq g_bfree_exec_transfer_rip(%rip), %rcx
	testq %rcx, %rcx
	jnz .bfree_syscall_exec_transfer_rip_ok
	movq g_bfree_sysret_exec_rcx(%rip), %rcx
.bfree_syscall_exec_transfer_rip_ok:

	movq g_bfree_sysret_exec_r11(%rip), %r11

	movq g_bfree_sysret_exec_rdi(%rip), %rdi

	movq g_bfree_sysret_exec_rsi(%rip), %rsi

	movq g_bfree_sysret_exec_rdx(%rip), %rdx

	/* Do not inherit init's syscall args / magic return in the new ring3 image. */

	xorq %rax, %rax

	xorq %r8, %r8

	xorq %r9, %r9

	xorq %r10, %r10

	sysretq



.bfree_syscall_thread_child:

	pop %rcx

	pop %r11

	pop %r15

	pop %rdi

	pop %rsi

	pop %rdx

	pop %r8

	pop %r9

	pop %r10

	xorq %rax, %rax

	movq g_bfree_sysret_exec_rsp(%rip), %rsp

	sysretq



# Minimal CATCH delivery / rt_sigreturn restore: RIP/RSP/RFLAGS/RDI/RSI/RDX/RAX
# plus callee-saved rbx/rbp/r12–r15 from g_bfree_sig_saved_* (H01).
.bfree_syscall_signal_deliver:

	pop %rcx

	pop %r11

	pop %r15

	pop %rdi

	pop %rsi

	pop %rdx

	pop %r8

	pop %r9

	pop %r10

	movq g_bfree_sig_saved_rbx(%rip), %rbx

	movq g_bfree_sig_saved_rbp(%rip), %rbp

	movq g_bfree_sig_saved_r12(%rip), %r12

	movq g_bfree_sig_saved_r13(%rip), %r13

	movq g_bfree_sig_saved_r14(%rip), %r14

	movq g_bfree_sig_saved_r15(%rip), %r15

	movq g_bfree_sysret_exec_rsp(%rip), %rsp

	movq g_bfree_sysret_exec_rcx(%rip), %rcx

	movq g_bfree_sysret_exec_r11(%rip), %r11

	movq g_bfree_sysret_exec_rdi(%rip), %rdi

	movq g_bfree_sysret_exec_rsi(%rip), %rsi

	movq g_bfree_sysret_exec_rdx(%rip), %rdx

	movq g_bfree_sysret_sig_rax(%rip), %rax

	xorq %r8, %r8

	xorq %r9, %r9

	xorq %r10, %r10

	sysretq


