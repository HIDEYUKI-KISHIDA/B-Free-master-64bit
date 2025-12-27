

// main.c - 64ビットカーネル雛形
#include "../include64/types.h"

#include <stdint.h>

// main.c - 64ビットカーネル雛形（拡充用）
#include "../include64/types.h"

// 雛形の初期化関数（中身は空、後で拡充）
void setup_gdt64(void) {}
void setup_idt64(void) {}
void init_memory64(void) {}
void console_init64(void) {}
void init_device64(void) {}
void setup_interrupts64(void) {}
void timer_init64(void) {}
void process_init64(void) {}
void syscall_init64(void) {}
void fs_init64(void) {}
void shell64_main(void) {}

// バナー表示（仮実装）
void banner(void) {
    const char *logo =
        "\nB-Free OS 64bit Kernel (stub)\n";
    for (const char *p = logo; *p; ++p) {
        // 仮の出力（本来はconsole_putc64等）
    }
}

// カーネル初期化処理
void kernel_init(void) {
    setup_gdt64();
    setup_idt64();
    init_memory64();
    console_init64();
    init_device64();
    setup_interrupts64();
    timer_init64();
    process_init64();
    syscall_init64();
    fs_init64();
}

// メインループ
void kernel_main(void) {
    kernel_init();
    banner();
    shell64_main();
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
// メインループ
