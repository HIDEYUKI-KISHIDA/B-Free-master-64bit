/*
 * idt.c - x86_64 T-Kernel2.0 IDT初期化
 * 仕様: TK2_x86_64_Spec.md v2.3準拠
 */
#include <stdint.h>
extern void uart_puts(const char*);

#define IDT_SIZE 256

/* idt_data.cで定義されたIDT本体を参照 */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));
extern struct idt_entry idt[256];

/* 外部: 割込みスタブ（interrupt.Sで定義） */


/* IDT登録関数 */
void knl_set_idt(int vector, uint64_t handler) {
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = 0x08;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = 0x8E;
    idt[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[vector].reserved    = 0;
}

/* IDTディスクリプタ */
struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_desc;

void knl_idt_init(void) {
    // IDT配列全体をゼロクリア（memset非依存）
    for (int i = 0; i < 256; ++i) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }
    uart_puts("[IDT] Initializing the interrupt descriptor table.\n");
    uart_puts("[IDT] Registering 256 vectors\n");
    /* 0〜255まで全てのベクタを登録 */
    extern void (*knl_interrupt_stubs[256])(void);
    for (int i = 0; i < 256; ++i) {
        knl_set_idt(i, (uint64_t)knl_interrupt_stubs[i]);
    }
    struct idtr {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed));
    /* グローバルなidt_descに変更 */
    static struct idtr idt_desc;
    idt_desc.limit = (uint16_t)(sizeof(idt) - 1); // 必ず4095(0x0FFF)
    idt_desc.base  = (uint64_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idt_desc)); // 有効化
    uart_puts("[IDT] Registration complete\n");
    uart_puts("[IDT] Interrupt handling is ready.\n");
}
