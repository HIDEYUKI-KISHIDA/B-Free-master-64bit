
#include <stdint.h>

#ifndef VGA_WIDTH
#define VGA_WIDTH 80
#endif
#ifndef VGA_HEIGHT
#define VGA_HEIGHT 25
#endif
#ifndef VGA_ADDRESS
#define VGA_ADDRESS 0xB8000
#endif

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void vga_set_cursor(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
// kernel/sysmain/vga_console.c
// 最小限のVGAテキストバッファ出力によるkputc実装

// シリアル出力用uart_putsのプロトタイプ宣言
void uart_puts(const char *s);

static uint16_t* const vga_buffer = (uint16_t*)VGA_ADDRESS;
uint8_t cursor_x = 0;
uint8_t cursor_y = 2; // 2行目（row=1）までvga_putsで埋めるので、その次から

void kputc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        if (++cursor_y >= VGA_HEIGHT) cursor_y = 0;
        vga_set_cursor(cursor_x, cursor_y);
        return;
    }
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
        vga_buffer[pos] = (0x07 << 8) | ' ';
        vga_set_cursor(cursor_x, cursor_y);
        return;
    }
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    vga_buffer[pos] = (0x07 << 8) | c;
    if (++cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        if (++cursor_y >= VGA_HEIGHT) cursor_y = 0;
    }
    vga_set_cursor(cursor_x, cursor_y);
}
