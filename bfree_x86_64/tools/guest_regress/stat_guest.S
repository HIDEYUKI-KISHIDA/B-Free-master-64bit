	.global _start
	.section .text
_start:
	sub $160, %rsp
	lea path(%rip), %rdi
	mov %rsp, %rsi
	mov $4, %rax
	syscall
	test %rax, %rax
	js fail
	mov 24(%rsp), %eax
	and $0xF000, %eax
	cmp $0x2000, %eax
	jne fail

	mov $-100, %rdi
	lea path(%rip), %rsi
	mov %rsp, %rdx
	xor %rcx, %rcx
	mov $262, %rax
	syscall
	test %rax, %rax
	js fail
	mov 24(%rsp), %eax
	and $0xF000, %eax
	cmp $0x2000, %eax
	jne fail

	mov $1, %rdi
	lea msg(%rip), %rsi
	mov $len, %rdx
	mov $1, %rax
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
	.ascii "STAT_GUEST_OK\n"
len = . - msg
