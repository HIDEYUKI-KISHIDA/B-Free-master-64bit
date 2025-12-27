// include_arm/types_arm.h
// ARM/64ビット・btron-pc共通型定義・定数集約
// 2025/12/27 共通化・整理

#ifndef TYPES_ARM_H
#define TYPES_ARM_H

#include <stdint.h>

// MMU/MPU サポート用型定義・定数
#define PAGE_SIZE      4096
#define PAGE_TABLE_ENTRIES 1024

typedef struct {
    uint32_t entries[PAGE_TABLE_ENTRIES];
} page_table_t;

// MMU制御用レジスタ定数（例: ARMv7）
#define MMU_CONTROL_ENABLE      (1 << 0)
#define MMU_CONTROL_ALIGN      (1 << 1)
#define MMU_CONTROL_DCACHE     (1 << 2)
#define MMU_CONTROL_ICACHE     (1 << 12)

// 共通型定義例
#ifndef BOOL_DEFINED
#define BOOL_DEFINED
    typedef int BOOL;
    #define TRUE 1
    #define FALSE 0
#endif

// 他アーキ共通型（必要に応じて追加）
typedef uint32_t phys_addr_t;
typedef uint32_t virt_addr_t;

#endif // TYPES_ARM_H
