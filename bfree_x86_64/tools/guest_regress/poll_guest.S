	.global _start
	.section .text
_start:
	sub $32, %rsp
	lea 16(%rsp), %rdi
	mov $22, %rax
	syscall
	test %rax, %rax
	js fail

	mov 24(%rsp), %rdi
	lea byte(%rip), %rsi
	mov $1, %rdx
	mov $1, %rax
	syscall
	test %rax, %rax
	js fail

	mov 16(%rsp), %eax
	mov %eax, (%rsp)
	mov $1, %ax
	mov %ax, 4(%rsp)
	mov %rsp, %rdi
	mov $1, %rsi
	xor %rdx, %rdx
	mov $7, %rax
	syscall
	cmp $1, %rax
	jne fail
	movzwl 6(%rsp), %eax
	and $1, %eax
	jz fail

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

byte:
	.ascii "x"
	.section .rodata
msg:
	.ascii "POLL_GUEST_OK\n"
len = . - msg
