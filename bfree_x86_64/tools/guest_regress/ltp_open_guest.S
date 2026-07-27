	.global _start
	.section .text
_start:
	lea path(%rip), %rdi
	mov $1, %rsi
	xor %rdx, %rdx
	mov $2, %rax
	syscall
	test %rax, %rax
	js fail
	mov %rax, %rbx

	mov %rbx, %rdi
	lea msg(%rip), %rsi
	mov $len, %rdx
	mov $1, %rax
	syscall

	mov %rbx, %rdi
	mov $3, %rax
	syscall

	xor %rdi, %rdi
	mov $60, %rax
	syscall

fail:
	mov $1, %rdi
	mov $60, %rax
	syscall

path:
	.asciz "/dev/console"
msg:
	.ascii "LTP_OPEN_GUEST_OK\n"
len = . - msg
