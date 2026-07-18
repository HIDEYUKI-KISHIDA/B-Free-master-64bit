#include <stdint.h>
#include <stddef.h>
#include <string.h>

// シンプルなデバイスツリー（FDT/ACPI風）自動記述・認識雛形

typedef struct devicetree_node {
    const char *name;
    uint32_t type;
    uint32_t addr;
    uint32_t irq;
    // ...拡張用...
} devicetree_node_t;

#define DEVICETREE_MAX_NODES 64
static devicetree_node_t devicetree_table[DEVICETREE_MAX_NODES];
static int devicetree_count = 0;

// ノード登録
int devicetree_register(const char *name, uint32_t type, uint32_t addr, uint32_t irq) {
    if (devicetree_count >= DEVICETREE_MAX_NODES) return -1;
    devicetree_table[devicetree_count].name = name;
    devicetree_table[devicetree_count].type = type;
    devicetree_table[devicetree_count].addr = addr;
    devicetree_table[devicetree_count].irq = irq;
    devicetree_count++;
    return 0;
}

// ノード検索
const devicetree_node_t *devicetree_find(const char *name) {
    for (int i = 0; i < devicetree_count; ++i) {
        if (strcmp(devicetree_table[i].name, name) == 0)
            return &devicetree_table[i];
    }
    return NULL;
}

// 今後: FDT/ACPIテーブル等から自動生成・自動認識機能を拡張
