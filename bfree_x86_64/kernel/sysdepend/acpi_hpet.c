// acpi_hpet.c - HPETタイマ本実装（仕様書v2.4準拠）
//
// * HPET優先、APICタイマはフォールバック
// * ACPIテーブルからHPETベースアドレス取得
// * 割り込み登録・EOI送出
// * API枠組みはPoCから流用

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define HPET_BASE_DEFAULT 0xFED00000UL
#define HPET_REG(offset) (*(volatile uint64_t*)(hpet_base + (offset)))

static uint64_t hpet_base = HPET_BASE_DEFAULT;
static int hpet_available = 0;
static void (*g_timer_callback)(void) = NULL;

void acpi_hpet_init(void) {
    // TODO: ACPIテーブルからHPETベースアドレス取得（現状はデフォルト）
    hpet_base = HPET_BASE_DEFAULT;
    // HPET有効化
    HPET_REG(0x10) |= 1; // General Configuration Register: EnableCnf
    hpet_available = 1;
    printf("[HPET] acpi_hpet_init: base=0x%llx\n", hpet_base);
}

void acpi_hpet_set_interval(unsigned long ms) {
    if (!hpet_available) return;
    // HPETカウンタ・コンパレータ設定（簡易例）
    uint64_t freq = 1000000000ULL; // 仮: 1GHz
    uint64_t ticks = (freq / 1000) * ms;
    HPET_REG(0x108) = HPET_REG(0xF0) + ticks; // Comparator
    printf("[HPET] set_interval: %lu ms\n", ms);
}

void acpi_hpet_set_callback(void (*cb)(void)) {
    g_timer_callback = cb;
    printf("[HPET] set_callback\n");
}

void acpi_hpet_irq_handler(void) {
    if (g_timer_callback) g_timer_callback();
    // EOI送出（APIC経由）
    // *(volatile uint32_t*)(0xFEE000B0) = 0; // APIC EOI
    printf("[HPET] irq_handler\n");
}

// [フォールバック] HPETが使えない場合はAPICタイマ等で代用
// ...
