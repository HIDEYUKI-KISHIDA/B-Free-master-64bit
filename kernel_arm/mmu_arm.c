// kernel_arm/mmu_arm.c
// ARM/64ビット メモリ管理・MMUコア雛形・API
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_PAGES 1024

static uint8_t page_bitmap[MAX_PAGES];

// MMU初期化
void mmu_init(void) {
    for (int i = 0; i < MAX_PAGES; ++i) page_bitmap[i] = 0;
    // QEMU/ARMv8向けページテーブル初期化雛形
    // 実際はL1/L2/L3テーブルを確保し、エントリを初期化
    // 例: page_table_base = alloc_page();
    // 必要に応じて仮想アドレス空間をマッピング
    // ...
    printf("[MMU] mmu_init: QEMU/ARMv8ページテーブル初期化(雛形)\n");
}

// ページ割当て
void *alloc_page(void) {
    for (int i = 0; i < MAX_PAGES; ++i) {
        if (!page_bitmap[i]) {
            page_bitmap[i] = 1;
            // QEMU/ARMv8向け物理アドレス計算例
            void *addr = (void *)(0x80000000 + i * 0x1000);
            printf("[MMU] alloc_page: 0x%p\n", addr);
            return addr;
        }
    }
    return NULL;
}

// ページ解放
void free_page(void *addr) {
    int idx = ((uintptr_t)addr - 0x80000000) / 0x1000;
    if (idx >= 0 && idx < MAX_PAGES) page_bitmap[idx] = 0;
}

// MMU有効化
void enable_mmu(void) {
    // QEMU/ARMv8向けMMU有効化雛形
    // 実際はSCTLR_EL1レジスタ等を操作
    printf("[MMU] enable_mmu: ARMv8 MMU有効化(雛形)\n");
    // 例: asm volatile ("msr sctlr_el1, ...");
}

// 仮想→物理変換
uintptr_t virt_to_phys(uintptr_t va) {
    // TODO: ページテーブル参照
    return va;
}
