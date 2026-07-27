/*
 * Ring-3 trampoline: BusyBox ash regress one-liner (M15).
 */
	.section .note.GNU-stack,"",@progbits
	.section .text
	.global ash_regress_tramp
	.global ash_regress_tramp_end
	.global ash_regress_entry_slot
ash_regress_tramp:
	mov $4, %rdi
	lea argv(%rip), %rsi
	lea envp(%rip), %rdx
	mov ash_regress_entry_slot(%rip), %rax
	jmp *%rax

	.align 8
argv:
	.quad str0
	.quad str1
	.quad str2
	.quad str3
	.quad 0
envp:
	.quad 0
ash_regress_entry_slot:
	.quad 0
str0:
	.asciz "/bin/busybox"
str1:
	.asciz "ash"
str2:
	.asciz "-c"
str3:
	.asciz "out=$(echo pipe-data | cat); [ \"$out\" = \"pipe-data\" ] || exit 1; echo ASH_PIPE_GUEST_OK; marker=parent; ( marker=subshell ); [ \"$marker\" = \"parent\" ] || exit 1; echo ASH_SUBSHELL_GUEST_OK; out=$(echo hello); [ \"$out\" = \"hello\" ] || exit 1; echo ASH_CMDSUBST_GUEST_OK; sleep 0 & wait; echo ASH_BG_GUEST_OK; /bin/true || exit 1; /bin/false && exit 1; echo ASH_EXTERNAL_GUEST_OK"

ash_regress_tramp_end:
