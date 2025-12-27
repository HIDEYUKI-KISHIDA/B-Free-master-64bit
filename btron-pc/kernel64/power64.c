// power64.c - 64ビット用 電源管理API雛形（ダミー実装）
#include "../include64/types.h"

void reboot64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("[POWER] Rebooting...\n");
    // 本来はリセット命令
}

void poweroff64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("[POWER] Powering off...\n");
    while (1) { __asm__ __volatile__("hlt"); }
}

void suspend64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("[POWER] Suspending...\n");
    // 本来はサスペンド命令
}
