/*
 * timer.c - x86_64 T-Kernel2.0 PITタイマ制御
 * 仕様: TK2_x86_64_Spec.md v2.3準拠
 */
#include <stdint.h>

// 外部関数プロトタイプ宣言（lapic/acpi_hpet 実装）
extern uint64_t acpi_get_hpet_base(void);
extern void lapic_enable(void);
extern void lapic_timer_setup(uint32_t count, uint8_t vector);
extern void lapic_eoi(void);


#define PIT_CTRL 0x43
#define PIT_CNT0 0x40
#define PIT_FREQ 1193182
#define PIT_HZ   1000  // 1msごと(1000Hz)


// HPET/Local APIC Timer用定義（雛形）
#define HPET_BASE 0xFED00000UL
#define HPET_ENABLE_CNF 0x1
#define HPET_TN_INT_ENB_CNF (1 << 2)
#define HPET_TN_TYPE_CNF   (1 << 3)
#define HPET_TN_VAL_SET_CNF (1 << 6)

int hpet_available = 0;
int lapic_available = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 外部関数 */
void knl_pic_eoi(int irq);

// timer_manager.cの論理タイマ割り込みハンドラを呼ぶ
#include "../../../sysmain/timer_manager.h"
void knl_timer_tick(void) {
    timer_handler();
}

// HPET初期化（仕様書v2.4準拠・本実装）
extern void uart_puts(const char*);
static int hpet_init(void) {
    uart_puts("hpet_init: 1 (acpi_get_hpet_base)\n");
    uint64_t hpet_base = acpi_get_hpet_base();
    uart_puts("hpet_init: 2 (check base)\n");
    if (!hpet_base) {
        uart_puts("hpet_init: 3 (base==0)\n");
        return 0;
    }
    uart_puts("hpet_init: 4 (hpet ptr/cap)\n");
    volatile uint64_t* hpet = (volatile uint64_t*)hpet_base;
    uint64_t cap = hpet[0x0]; // HPET_CAP_ID
    uart_puts("hpet_init: 5 (period_fs)\n");
    uint64_t period_fs = cap >> 32;
    if (period_fs == 0 || period_fs > 100000000ULL) {
        uart_puts("hpet_init: 6 (period_fs invalid)\n");
        return 0;
    }
    uart_puts("hpet_init: 7 (ticks calc)\n");
    uint64_t ticks = (1000000ULL * 1000000ULL) / period_fs;
    uart_puts("hpet_init: 8 (timer0 config)\n");
    hpet[0x100 / 8] = (1ULL << 2) | (1ULL << 3) | (1ULL << 6);
    uart_puts("hpet_init: 9 (set cmp)\n");
    hpet[0x108 / 8] = ticks;
    uart_puts("hpet_init: 10 (enable main counter)\n");
    hpet[0x10 / 8] |= 1; // HPET_CONFIG: メインカウンタ有効化
    uart_puts("hpet_init: 11 (done)\n");
    // IOAPIC経由でHPET割り込みをルーティング（例: knl_ioapic_route(HPET_IRQ, TIMER_VECTOR, 0)）
    // 必要に応じて割り込みベクタ・IRQ番号を定義
    return 1;
}

// Local APIC Timer初期化（雛形）
static int lapic_timer_init(void) {
    lapic_enable();
    // 割り込みベクタ0x40, カウント値は仮で10000000
    lapic_timer_setup(10000000, 0x40);
    return 1; // 成功
}

extern void uart_puts(const char*);
void knl_timer_init(void) {
    uart_puts("knl_timer_init: 1 (HPET)\n");
    if (hpet_init()) {
        uart_puts("knl_timer_init: 2 (HPET OK)\n");
        hpet_available = 1;
        return;
    }
    uart_puts("knl_timer_init: 3 (LAPIC)\n");
    if (lapic_timer_init()) {
        uart_puts("knl_timer_init: 4 (LAPIC OK)\n");
        lapic_available = 1;
        return;
    }
    uart_puts("knl_timer_init: 5 (PIT)\n");
    // フォールバック: PIT
    uint16_t count = PIT_FREQ / PIT_HZ;
    outb(PIT_CTRL, 0x36); // モード3(方形波), カウンタ0, LSB→MSB
    outb(PIT_CNT0, count & 0xFF); // LSB
    outb(PIT_CNT0, (count >> 8) & 0xFF); // MSB
    uart_puts("knl_timer_init: 6 (PIT OK)\n");
}

void knl_timer_handler(void) {
    knl_timer_tick();
    if (hpet_available) {
        // HPET割り込みクリア処理（必要に応じて実装）
    } else if (lapic_available) {
        lapic_eoi();
    } else {
        knl_pic_eoi(0); // IRQ0 (PIT) のEOI
    }
}
