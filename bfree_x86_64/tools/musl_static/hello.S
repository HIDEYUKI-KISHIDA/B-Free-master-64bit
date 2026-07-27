	.global _start
	.section .text
_start:
	mov $1, %rax
	mov $1, %rdi
	lea msg(%rip), %rsi
	mov $12, %rdx
	syscall
	xor %rdi, %rdi
	mov $60, %rax
	syscall
msg:
	.ascii "MUSL_STATIC\n"
