
#include "vram.h"
#include <stdint.h>

#define MAX_WIDTH 79
#define MAX_HEIGHT 24

// PC-98 テキストVRAMは0xA0000、I/Oポート0xA1, 0xA3で制御
#define PC98_VRAM_ADDR ((volatile uint8_t*)0xA0000)
#define PC98_SCREEN_WIDTH 80

// I/Oポート出力（x86向け）
static inline void outb(uint16_t port, uint8_t val) {
#if defined(__GNUC__)
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}

void write_vram(int x, int y, int ch, int attr) {
    if (x < 0 || x > MAX_WIDTH || y < 0 || y > MAX_HEIGHT) return;
    // PC-98: テキストVRAMは1バイト/文字、属性はI/Oポートで制御
    PC98_VRAM_ADDR[x + y * PC98_SCREEN_WIDTH] = ch & 0xFF;
    // 属性（色）はI/Oポート0xA1で設定（簡易実装: 全体色一括）
    outb(0xA1, attr & 0xFF);
}

void set_cursor_pos(int x, int y) {
    uint16_t pos = x + y * PC98_SCREEN_WIDTH;
    // PC-98: カーソル位置はI/Oポート0xA3, 0xA5で設定
    outb(0xA3, pos & 0xFF);      // 下位8bit
    outb(0xA5, (pos >> 8) & 0xFF); // 上位8bit
}

void scroll_up() {
    // 1行上に詰める
    for (int y = 1; y <= MAX_HEIGHT; y++) {
        for (int x = 0; x <= MAX_WIDTH; x++) {
            PC98_VRAM_ADDR[x + (y - 1) * PC98_SCREEN_WIDTH] = PC98_VRAM_ADDR[x + y * PC98_SCREEN_WIDTH];
        }
    }
    // 最下行を空白で埋める
    for (int x = 0; x <= MAX_WIDTH; x++) {
        PC98_VRAM_ADDR[x + MAX_HEIGHT * PC98_SCREEN_WIDTH] = ' ';
    }
}

void write_kanji_vram(int x, int y, unsigned int ch, int attr) {
    // PC-98: 2バイト文字の上位・下位を連続で出力
    write_vram(x, y, (ch >> 8) & 0xFF, attr);
    if (x < MAX_WIDTH)
        write_vram(x + 1, y, ch & 0xFF, attr);
}
