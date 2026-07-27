	.text
	.global _start
_start:
	mov $0xe9, %dx
	lea msg(%rip), %rsi
1:
	movb (%rsi), %al
	test %al, %al
	je 2f
	out %al, %dx
	inc %rsi
	jmp 1b
2:
	hlt
	jmp 2b
msg:
	.asciz "BOOT_OK\n"
