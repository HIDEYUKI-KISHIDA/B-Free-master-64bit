/*
 * LEGACY — not linked into kernel.elf (M30).
 * BusyBox ash boots via Linux stack+auxv (ash_guest_boot.c), not this C-ABI trampoline.
 *
 * Ring-3 trampoline: set Linux argc/argv/envp then jump to BusyBox entry (M12).
 */
	.section .note.GNU-stack,"",@progbits
	.section .text
	.global ash_guest_tramp
	.global ash_guest_tramp_end
	.global ash_guest_entry_slot
ash_guest_tramp:
	mov $4, %rdi
	lea argv(%rip), %rsi
	lea envp(%rip), %rdx
	mov ash_guest_entry_slot(%rip), %rax
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
ash_guest_entry_slot:
	.quad 0
str0:
	.asciz "/bin/busybox"
str1:
	.asciz "ash"
str2:
	.asciz "-c"
str3:
	.asciz "echo ASH_GUEST_OK"

ash_guest_tramp_end:
