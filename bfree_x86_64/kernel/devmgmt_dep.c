#include <stdint.h>
#include "devmgmt.c"

// デバイス依存関係・優先度・リソース競合解決の自動化雛形

// 依存関係チェック
int devmgmt_check_dependency(uint32_t device_id) {
    for (int i = 0; i < devmgmt_count; ++i) {
        if (devmgmt_table[i].device_id == device_id) {
            return devmgmt_table[i].dependency;
        }
    }
    return -1;
}

// 優先度比較
int devmgmt_compare_priority(uint32_t dev1, uint32_t dev2) {
    int p1 = -1, p2 = -1;
    for (int i = 0; i < devmgmt_count; ++i) {
        if (devmgmt_table[i].device_id == dev1) p1 = devmgmt_table[i].priority;
        if (devmgmt_table[i].device_id == dev2) p2 = devmgmt_table[i].priority;
    }
    if (p1 < 0 || p2 < 0) return 0;
    return (p1 > p2) ? 1 : (p1 < p2) ? -1 : 0;
}

// 今後: 競合解決・自動割当・依存グラフ構築等を拡張
