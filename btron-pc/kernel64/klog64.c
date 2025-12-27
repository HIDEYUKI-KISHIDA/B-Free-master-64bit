// klog64.c - 64ビット用 カーネルデバッグ/トレース雛形（ダミー実装）
#include "../include64/types.h"

#define KLOG_SIZE64 256
static char klog_buf[KLOG_SIZE64][64];
static int klog_pos = 0;

void klog64(const char *msg) {
    strncpy(klog_buf[klog_pos], msg, 63);
    klog_buf[klog_pos][63] = 0;
    klog_pos = (klog_pos + 1) % KLOG_SIZE64;
}

void klog_dump64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    for (int i = 0; i < KLOG_SIZE64; i++) {
        if (klog_buf[i][0]) {
            puts64(klog_buf[i]);
            puts64("\n");
        }
    }
}

void printk64(const char *msg) {
    klog64(msg);
    // 画面にも出力
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64(msg);
}

void trace64(const char *msg) {
    klog64(msg);
}
