
// interrupt64.c - 64ビット用 割り込み・例外ハンドラ雛形（本物雰囲気）
#include "../include64/types.h"

#define IDT_SIZE 256

typedef void (*isr64_t)(void);

typedef struct {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char type_attr;
    unsigned short offset_mid;
    unsigned int offset_high;
    unsigned int zero;
} __attribute__((packed)) idt_entry64_t;

static idt_entry64_t idt64[IDT_SIZE];
static isr64_t isr_table64[IDT_SIZE];

// 雰囲気: IDTエントリ設定
void set_idt_entry64(int vec, isr64_t handler) {
    isr_table64[vec] = handler;
    // 本来はhandlerアドレスをIDTエントリに分割格納
    // ここでは雰囲気のみ
}

// デフォルト割り込みハンドラ
void default_interrupt_handler64(void) {
    extern void printk64(const char*);
    printk64("[INTERRUPT] Interrupt occurred\n");
}

// 例外/IRQ番号ごとの分岐雰囲気
void interrupt_dispatch64(int vec) {
    if (isr_table64[vec]) {
        isr_table64[vec]();
    } else {
        default_interrupt_handler64();
    }
}

void setup_interrupts64(void) {
    extern void printk64(const char*);
    // 例外0-31: 例外ハンドラ登録（雰囲気）
    for (int i = 0; i < 32; i++) {
        set_idt_entry64(i, default_interrupt_handler64);
    }
    // IRQ 32-47: IRQハンドラ登録（雰囲気）
    for (int i = 32; i < 48; i++) {
        set_idt_entry64(i, default_interrupt_handler64);
    }
    // その他は全てデフォルト
    for (int i = 48; i < IDT_SIZE; i++) {
        set_idt_entry64(i, default_interrupt_handler64);
    }
    printk64("[INTERRUPT] IDT setup done\n");
}
