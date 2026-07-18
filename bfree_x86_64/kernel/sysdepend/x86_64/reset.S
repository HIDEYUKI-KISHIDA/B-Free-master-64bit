
.section .text
.global _start
.type _start, @function
_start:
	mov $0xb8000, %edi

			.extern idt


	# --- IDTポインタ（.data） ---
	.section .data
	.align 8
	idt_ptr:
	    .word 256*16-1     # limit
	    .quad idt          # base

	# --- IDT初期化関数（lidt実行） ---
	.section .text
	.global init_idt
	init_idt:
	    lea idt_ptr(%rip), %rax
	    lidt (%rax)
	    ret

	# 旧テスト表示・シリアル出力部分（参考用にコメントアウト）
	# mov $0x4f4b, (%edi)             # 'K'（0x4b）を赤背景・白文字で表示
	# hlt
	# 1:
	#     jmp 1b
	#     # シリアル出力 (COM1: 0x3F8)
	#     mov $0x3F8, %dx
	#     mov $'K', %al
	#     out %al, %dx

	# --- スタック領域（.bss） ---
	.section .bss
	.align 16
	stack_bottom:           # スタックの開始地点（低アドレス）
		.skip 16384         # 16KBの領域を確保
	.global knl_kernel_stack_top
	knl_kernel_stack_top:   # スタックの頂上（高アドレス）。RSPの初期値

.section .note.GNU-stack,"",@progbits
