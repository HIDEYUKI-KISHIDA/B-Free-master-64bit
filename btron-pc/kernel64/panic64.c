// panic64.c - 64ビット用 カーネルパニック/例外ハンドラ雛形
#include "../include64/types.h"

void panic64(const char *msg) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    puts64("[KERNEL PANIC] ");
    puts64(msg);
    puts64("\nSystem halted.\n");
    while (1) { __asm__ __volatile__("hlt"); }
}

void abort64(const char *msg) {
    panic64(msg);
}

void exception_handler64(int code) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Exception %d occurred\n", code);
    panic64(buf);
}
