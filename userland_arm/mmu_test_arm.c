// userland_arm/mmu_test_arm.c
// MMU/仮想メモリ動作テスト用サンプル
// 2025/12/27 新規作成

#include "../../btron-pc/include_arm/types_arm.h"
#include <stdio.h>

extern void mmu_init(void);
extern void enable_mmu(void);
extern void disable_mmu(void);
extern uintptr_t virt_to_phys(uintptr_t va);

int main(void) {
    printf("MMUテスト開始\n");
    mmu_init();
    enable_mmu();

    uintptr_t va = 0x1000;
    uintptr_t pa = virt_to_phys(va);
    printf("仮想アドレス0x%lx → 物理アドレス0x%lx\n", va, pa);

    disable_mmu();
    printf("MMUテスト終了\n");
    return 0;
}
