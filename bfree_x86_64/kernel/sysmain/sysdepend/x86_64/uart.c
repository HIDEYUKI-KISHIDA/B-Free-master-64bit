/*
 * uart.c - x86_64 T-Kernel2.0 デバッグ用UART出力
 * 仕様: TK2_x86_64_Spec.md v2.3準拠
 */
#include <stdint.h>

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void uart_init(uint32_t baud) {
    uint16_t divisor = 115200 / (baud ? baud : 115200);
    outb(COM1_PORT + 1, 0x00);    // 割込み禁止
    outb(COM1_PORT + 3, 0x80);    // DLAB=1
    outb(COM1_PORT + 0, divisor & 0xFF);      // ボーレート下位
    outb(COM1_PORT + 1, (divisor >> 8) & 0xFF); // ボーレート上位
    outb(COM1_PORT + 3, 0x03);    // 8bit, ノンパリティ, 1ストップ, DLAB=0
    outb(COM1_PORT + 2, 0xC7);    // FIFO有効, 14byteトリガ
    outb(COM1_PORT + 4, 0x0B);    // RTS/DSR/OUT2
}

void uart_putc(char c) {
    while (!(inb(COM1_PORT + 5) & 0x20)) ; // 送信バッファ空き待ち
    outb(COM1_PORT, (uint8_t)c);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
