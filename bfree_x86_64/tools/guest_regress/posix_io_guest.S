	.global _start
	.section .text
_start:
	sub $16, %rsp
	mov %rsp, %rdi
	mov $22, %rax
	syscall
	test %rax, %rax
	js fail

	mov 8(%rsp), %rdi
	lea byte(%rip), %rsi
	mov $1, %rdx
	mov $1, %rax
	syscall
	test %rax, %rax
	js fail

	mov (%rsp), %rdi
	lea buf(%rip), %rsi
	mov $1, %rdx
	xor %rax, %rax
	syscall
	cmp $1, %rax
	jne fail

	mov buf(%rip), %al
	cmp $'x', %al
	jne fail

	mov $1, %rdi
	lea okmsg(%rip), %rsi
	mov $oklen, %rdx
	mov $1, %rax
	syscall

	xor %rdi, %rdi
	mov $60, %rax
	syscall

fail:
	mov $1, %rdi
	mov $60, %rax
	syscall

byte:
	.ascii "x"
	.section .bss
buf:
	.space 1
	.section .rodata
okmsg:
	.ascii "POSIX_IO_GUEST_OK\n"
oklen = . - okmsg
