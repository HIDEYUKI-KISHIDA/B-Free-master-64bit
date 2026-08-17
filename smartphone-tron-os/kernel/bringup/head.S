/*
 * STOS AArch64 bring-up: Linux ARM64 Image header + EL drop + UART 'S'.
 * Spec v0.5.1 §5.2 / §5.3.
 */
#include "board.h"

	.section .text.head, "ax"
	.align  2
	.global _start
_start:
	b	stos_entry
	.long	0
	.quad	0			/* text_offset */
	.quad	_end - _start		/* image_size */
	.quad	0xA			/* LE, 4K pages, place anywhere */
	.quad	0
	.quad	0
	.quad	0
	.long	0x644d5241		/* "ARM\x64" */
	.long	0

stos_entry:
	msr	daifset, #0xf

	/* EL2 → EL1 if needed (§5.3 / §6.2). */
	mrs	x0, CurrentEL
	cmp	x0, #0x8
	b.ne	1f
	msr	sctlr_el1, xzr
	mov	x0, #(1 << 31)		/* HCR_EL2.RW = AArch64 EL1 */
	msr	hcr_el2, x0
	mov	x0, #3			/* EL1/0 arch timer access */
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr
	mov	x0, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x0
	adr	x0, 1f
	msr	elr_el2, x0
	eret
1:
	ldr	x0, =stos_stack_top
	mov	sp, x0

	/* UART first chirp 'S' before BSS / C. */
	mov	w0, #'S'
	ldr	x1, =STOS_UART_DBG_BASE
#if defined(STOS_BOARD_LENA)
	str	w0, [x1, #0x700]
#else
	str	w0, [x1]
#endif

	/* Clear BSS. */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
	mov	x2, #0
2:
	cmp	x0, x1
	b.hs	3f
	str	x2, [x0], #8
	b	2b
3:
	bl	stos_main
4:
	wfi
	b	4b
