/*
 * ioapic.c - x86_64 I/O APIC 制御（雛形）
 * 仕様: TK2_x86_64_Spec.md v2.4準拠
 */
#include <stdint.h>

#define IOAPIC_BASE 0xFEC00000UL
#define IOAPIC_REGSEL (*(volatile uint32_t *)(IOAPIC_BASE + 0x00))
#define IOAPIC_WINDOW (*(volatile uint32_t *)(IOAPIC_BASE + 0x10))

static inline void ioapic_write(uint8_t reg, uint32_t value) {
    IOAPIC_REGSEL = reg;
    IOAPIC_WINDOW = value;
}

static inline uint32_t ioapic_read(uint8_t reg) {
    IOAPIC_REGSEL = reg;
    return IOAPIC_WINDOW;
}

void ioapic_set_redtbl(int irq, uint64_t data) {
    // Redirection Table: 0x10 + 2*irq (low), 0x10 + 2*irq+1 (high)
    ioapic_write(0x10 + 2*irq, (uint32_t)(data & 0xFFFFFFFF));
    ioapic_write(0x10 + 2*irq + 1, (uint32_t)(data >> 32));
}

// --- 主要デバイス用IRQハンドラ雛形 ---
#include "cpu_init.h" // register_irq_handler宣言用（必要に応じてパス調整）

static void timer_irq_handler(void *regs, int irq, uint64_t errcode) {
    (void)regs;
    (void)irq;
    (void)errcode;
    // タイマ割り込み処理（例: tickカウント、スケジューラ呼び出し等）
    // ...
}

static void keyboard_irq_handler(void *regs, int irq, uint64_t errcode) {
    (void)regs;
    (void)irq;
    (void)errcode;
    // キーボード割り込み処理（例: スキャンコード取得等）
    // ...
}

void ioapic_init(void) {
    // PIT(IRQ0)→INT 0x20、キーボード(IRQ1)→INT 0x21 など
    ioapic_set_redtbl(0, 0x20); // PIT
    ioapic_set_redtbl(1, 0x21); // Keyboard
    // 必要に応じて他デバイスも追加

    // 個別IRQハンドラ登録例（cpu_init.c側で呼び出し推奨）
    register_irq_handler(0, timer_irq_handler);      // IRQ0: PIT
    register_irq_handler(1, keyboard_irq_handler);   // IRQ1: Keyboard
}
