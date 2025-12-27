// kernel_arm/uart_arm.c
// ARM/64ビット UART/標準入出力・端末管理雛形・API
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stddef.h>

// UART初期化
void uart_init(void) {
    // TODO: UARTレジスタ初期化
    // TODO: ボーレート/パリティ/割り込み/フロー制御設定
    // TODO: 実機依存の初期化処理（例: PL011, 16550, QEMU等）
}

// 1文字送信
void uart_putc(char c) {
    // TODO: UART送信レジスタ書き込み
    // TODO: 送信バッファ空き待ち、割り込み駆動対応
}

// 1文字受信
char uart_getc(void) {
    // TODO: UART受信レジスタ読み出し
    // TODO: 受信バッファ/割り込み駆動対応
    return 0;
}

// 文字列送信
void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

// 仮想端末(TTY)管理雛形
void tty_init(void) {
    // TODO: TTY構造体・バッファ初期化
    // TODO: 入出力バッファリング、端末属性管理
}

void tty_write(const char *s) {
    // TODO: 仮想端末バッファ経由で出力
    uart_puts(s);
}

void tty_read(char *buf, size_t len) {
    // TODO: 仮想端末バッファ経由で入力
    for (size_t i = 0; i < len; ++i) buf[i] = uart_getc();
}
