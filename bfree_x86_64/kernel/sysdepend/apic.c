// apic.c - APIC初期化・IPI送信（仕様書v2.4準拠）
//
// * Local APIC有効化・8259A PIC無効化・APICベースアドレス取得
// * GAS(AT&T)構文でのMSR操作例あり
// * I/O APICやx2APIC拡張は必要に応じて追加

#include <stdint.h>
#include <stddef.h>

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_DEFAULT  0xFEE00000UL

static volatile uint32_t* apic_base = (volatile uint32_t*)APIC_BASE_DEFAULT;
static int cpu_count = 1; // 仮: 1コアのみ

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void disable_pic(void) {
    // 8259A PICを完全に無効化
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);
}

void apic_enable(void) {
    uint64_t apic_base_msr;
    // APICベースアドレス取得・有効化（MSR操作）
    asm volatile (
        "rdmsr" : "=A"(apic_base_msr) : "c"(IA32_APIC_BASE_MSR)
    );
    apic_base_msr |= (1ULL << 11); // APIC Global Enable
    asm volatile (
        "wrmsr" : : "c"(IA32_APIC_BASE_MSR), "A"(apic_base_msr)
    );
    apic_base = (volatile uint32_t*)(apic_base_msr & 0xFFFFF000UL);
}

void apic_init(void) {
    disable_pic();
    apic_enable();
    // 必要に応じてAPICレジスタ初期化（SPIV, LVT, TPR等）を追加
    // SMP対応時はACPI/MADT等でcpu_countを取得
    cpu_count = 1; // 仮: 1コアのみ
}

void apic_send_ipi(int cpu_id) {
    // APIC ICRレジスタ経由でIPI送信（SMP未対応のため未実装）
    (void)cpu_id;
    // 実装例: apic_base[0x300/4] = ...
}

int get_cpu_count(void) {
    return cpu_count;
}
