	.section .note.GNU-stack,"",@progbits
	.text
	.global _start
_start:
	mov $stack_top, %rsp
	and $-16, %rsp
	call bfree_kernel_boot
1:
	hlt
	jmp 1b

	.section .bss
	.align 16
stack:
	.space 16384
stack_top:
