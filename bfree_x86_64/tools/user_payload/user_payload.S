	.section .note.GNU-stack,"",@progbits
	.text
	.global _start
_start:
	mov $1, %rax
	mov $1, %rdi
	lea msg(%rip), %rsi
	mov $13, %rdx
	syscall
	mov $60, %rax
	xor %rdi, %rdi
	syscall
1:
	hlt
	jmp 1b
msg:
	.ascii "USER_BOOT_OK\n"
