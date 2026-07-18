/*
 * lapic.c - x86_64 Local APIC 制御（雛形）
 * 仕様: TK2_x86_64_Spec.md v2.4準拠
 */
#include <stdint.h>


extern uint32_t acpi_get_lapic_addr(void);
extern void uart_puts(const char*);
static inline volatile uint32_t* get_lapic_reg(uint32_t offset) {
    uint32_t base = acpi_get_lapic_addr();
    // デバッグ: LAPICベースアドレス出力
    static int shown = 0;
    if (!shown) { shown = 1;
        uart_puts("LAPIC_BASE (ACPI): 0x");
        for (int i = 7; i >= 0; --i) {
            int d = (base >> (i*4)) & 0xF;
            uart_puts((char[]){(char)(d<10?'0'+d:'A'+d-10),0});
        }
        uart_puts("\n");
    }
    return (volatile uint32_t*)(uintptr_t)(base + offset);
}
#define LAPIC_REG(offset) (*get_lapic_reg(offset))

#define LAPIC_ID         0x20
#define LAPIC_EOI        0xB0
#define LAPIC_SVR        0xF0
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CURR 0x390
#define LAPIC_TIMER_DIV  0x3E0

void lapic_eoi(void) {
    LAPIC_REG(LAPIC_EOI) = 0;
}

void lapic_enable(void) {
    LAPIC_REG(LAPIC_SVR) |= 0x100; // APIC Software Enable
}

void lapic_timer_setup(uint32_t initial_count, uint8_t vector) {
    LAPIC_REG(LAPIC_TIMER_DIV) = 0x3; // Divide by 16
    LAPIC_REG(LAPIC_LVT_TIMER) = vector;
    LAPIC_REG(LAPIC_TIMER_INIT) = initial_count;
}
