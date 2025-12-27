	.section .text
	.global _start
_start:
	/* スタック初期化（例: r13=0x8000） */
	ldr sp, =0x8000

	/* BSSクリア等（省略可） */

	/* Cエントリ呼び出し */
	bl kernel_main

	/* 無限ループ */
1: b 1b

/* Cエントリポイント */
.global kernel_main
kernel_main:
	/* ここにC側の初期化処理を記述 */
	b .
