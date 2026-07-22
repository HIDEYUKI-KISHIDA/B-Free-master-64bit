/*
 * Ring-3 exec trampoline with patchable argc/argv/envp/entry slots (M15).
 */
	.section .note.GNU-stack,"",@progbits
	.section .text
	.global guest_exec_tramp
	.global guest_exec_tramp_end
	.global guest_exec_argc_slot
	.global guest_exec_argv_slot
	.global guest_exec_envp_slot
	.global guest_exec_entry_slot
guest_exec_tramp:
	mov guest_exec_argc_slot(%rip), %rdi
	mov guest_exec_argv_slot(%rip), %rsi
	mov guest_exec_envp_slot(%rip), %rdx
	mov guest_exec_entry_slot(%rip), %rax
	jmp *%rax

	.align 8
guest_exec_argc_slot:
	.quad 0
guest_exec_argv_slot:
	.quad 0
guest_exec_envp_slot:
	.quad 0
guest_exec_entry_slot:
	.quad 0

guest_exec_tramp_end:
