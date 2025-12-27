// ARMカーネル開発 優先タスク2: MMU/MPUサポート
// kernel_arm/arch/mmu_arm.c
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>

// MMU初期化
void mmu_init(void) {
    init_page_table();
    // TODO: MMU制御レジスタ初期化（例: ARMv7/ARMv8）
    // - ページテーブルベースアドレス設定
    // - キャッシュ/属性/権限設定
    // - CP15レジスタ操作
}

// MMU有効化
void enable_mmu(void) {
#if defined(__arm__) || defined(__aarch64__)
    unsigned int val;
    // CP15 System Control Register (SCTLR) 読み出し
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(val));
    val |= MMU_CONTROL_ENABLE;
    // 書き戻し
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(val));
#endif
    // TODO: ARMv8以降はTTBR0/TTBR1/MAIR/属性設定も必要
}

// MMU無効化
void disable_mmu(void) {
#if defined(__arm__) || defined(__aarch64__)
    unsigned int val;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(val));
    val &= ~MMU_CONTROL_ENABLE;
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(val));
#endif
    // TODO: キャッシュ/属性/権限のクリアも考慮
}

// ページテーブル初期化
void init_page_table(void) {
    static page_table_t kernel_page_table;
    for (int i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        // 仮想=物理の1:1マッピング（例）
        // TODO: 実際は属性/権限/キャッシュ制御も設定
        kernel_page_table.entries[i] = (i << 12) | 0x12; // 仮: RW, cacheable
    }
    // TODO: ページテーブルベースアドレスをMMUに登録
}

// 仮想アドレス→物理アドレス変換
uintptr_t virt_to_phys(uintptr_t va) {
    // TODO: ページテーブル参照による変換
    // 仮: 1:1マッピング
    return va;
}
