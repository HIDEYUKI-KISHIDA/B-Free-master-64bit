// ARMカーネル開発 優先タスク1: 割り込みコントローラ（GIC等）初期化・管理
// kernel_arm/arch/interrupt_arm.c
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>

// GIC（Generic Interrupt Controller）初期化
void gic_init(void) {
    // GICレジスタ初期化例（実際のアドレス・値はSoC依存）
    volatile unsigned int *gicd_base = (unsigned int *)0x1E001000; // 例: GIC Distributor base
    volatile unsigned int *gicc_base = (unsigned int *)0x1E000100; // 例: GIC CPU Interface base
    // GIC Distributor enable
    gicd_base[0] = 0x1;
    // GIC CPU Interface enable
    gicc_base[0] = 0x1;
}

// 割り込み有効化
void enable_interrupts(void) {
    // TODO: ARMコアの割り込み有効化処理
}

// 割り込み無効化
void disable_interrupts(void) {
    // TODO: ARMコアの割り込み無効化処理
}

// 割り込みハンドラ配列
#define MAX_IRQS 128
static void (*irq_handlers[MAX_IRQS])(void) = {0};

// 割り込みハンドラ登録
void register_interrupt_handler(int irq, void (*handler)(void)) {
    if (irq >= 0 && irq < MAX_IRQS) {
        irq_handlers[irq] = handler;
    }
}

// 割り込みハンドラ呼び出し
void interrupt_handler_dispatch(int irq) {
    if (irq >= 0 && irq < MAX_IRQS && irq_handlers[irq]) {
        irq_handlers[irq]();
    }
}
