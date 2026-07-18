// paging.c - ページテーブル初期化（仕様書v2.4準拠）
//
// * boot_pml4/boot_pdpt/boot_pdは必ず.data配置
// * 16バイトアラインメント明示
// * paging_init()で初期化・CR3セット
// * GAS(AT&T)構文でのCR3ロード例はコメント参照

#include <stdint.h>
#include <stddef.h>

// 16バイトアラインメントを明示
__attribute__((section(".data.boot_pml4"), aligned(4096)))
uint64_t boot_pml4[512] = {0};
__attribute__((section(".data.boot_pml4"), aligned(4096)))
uint64_t boot_pdpt[512] = {0};
__attribute__((section(".data.boot_pml4"), aligned(4096)))
uint64_t boot_pd[512]   = {0};

void paging_init(void) {
    // 2MiBページ×512 = 1GiB分の物理メモリをidentity map
    // PML4[0] → PDPT[0] → PD[0..N]（PSビット使用）
    boot_pml4[0] = ((uint64_t)boot_pdpt) | 0x03; // RW|P
    boot_pdpt[0] = ((uint64_t)boot_pd)   | 0x03; // RW|P
    for (int i = 0; i < 512; ++i) {
        boot_pd[i] = (i * 0x200000ULL) | 0x83; // 2MiBページ, PS|RW|P
    }
    // --- HPET MMIO領域 (0xFED00000) をidentity map ---
    // 0xFED00000は2MiBページ境界内なので、対応するエントリをRW|P|PSで上書き
    // 0xFED00000 / 0x200000 = 0x7F6 = 2038
    boot_pd[2038] = (0x7F6 * 0x200000ULL) | 0x83; // 0xFED00000 identity map
    // CR3にPML4物理アドレスをセット（GAS構文例: movq $boot_pml4, %cr3）
    // 実際は物理アドレスに変換が必要な場合あり
    asm volatile ("movq %0, %%cr3" :: "r"(boot_pml4));
}

// 必要に応じてalloc_page/free_page等のAPIも追加
