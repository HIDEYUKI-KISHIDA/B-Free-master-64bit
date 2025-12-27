// kernel_arm/irq_arm.c
// ARM/64ビット 割り込み・タイマコア雛形・API
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_IRQ 64

static void (*irq_handlers[MAX_IRQ])(void);

// 割り込みコントローラ初期化
void gic_init(void) {
    for (int i = 0; i < MAX_IRQ; ++i) irq_handlers[i] = NULL;
    // QEMU/ARMv8向けGICレジスタ初期化雛形
    // 実際はGICD/GICC等のレジスタを初期化
    printf("[IRQ] gic_init: QEMU/ARMv8 GIC初期化(雛形)\n");
}

// 割り込みハンドラ登録
void register_interrupt_handler(int irq, void (*handler)(void)) {
    if (irq >= 0 && irq < MAX_IRQ) irq_handlers[irq] = handler;
}

// 割り込み有効化
void enable_interrupts(void) {
    // QEMU/ARMv8向け割り込み有効化雛形
    printf("[IRQ] enable_interrupts: ARMv8 割り込み有効化(雛形)\n");
    // 例: asm volatile ("msr daifclr, #2");
}

// タイマ初期化
void timer_init_arm(void) {
    // QEMU/ARMv8向けタイマ初期化雛形
    printf("[TIMER] timer_init_arm: ARMv8タイマ初期化(雛形)\n");
    // 例: タイマレジスタ設定
}

// タイマ割り込みハンドラ（例）
void timer_irq_handler(void) {
    // QEMU/ARMv8向けタイマ割り込み処理雛形
    printf("[TIMER] timer_irq_handler: 割り込み発生(雛形)\n");
    // 例: 割り込みフラグクリア等
}
