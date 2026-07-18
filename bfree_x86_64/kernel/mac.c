#include <stdint.h>

// セキュリティ強化（MAC/SELinux的な仕組み、カーネル空間保護）雛形

typedef enum {
    MAC_LEVEL_NONE,
    MAC_LEVEL_USER,
    MAC_LEVEL_ADMIN,
    MAC_LEVEL_SYSTEM
} mac_level_t;

typedef struct {
    uint32_t pid;
    mac_level_t level;
} mac_proc_t;

#define MAC_MAX_PROCS 64
static mac_proc_t mac_table[MAC_MAX_PROCS];
static int mac_count = 0;

// プロセスのMACレベル設定
int mac_set_level(uint32_t pid, mac_level_t level) {
    for (int i = 0; i < mac_count; ++i) {
        if (mac_table[i].pid == pid) {
            mac_table[i].level = level;
            return 0;
        }
    }
    if (mac_count < MAC_MAX_PROCS) {
        mac_table[mac_count].pid = pid;
        mac_table[mac_count].level = level;
        mac_count++;
        return 0;
    }
    return -1;
}

// MACレベル取得
mac_level_t mac_get_level(uint32_t pid) {
    for (int i = 0; i < mac_count; ++i) {
        if (mac_table[i].pid == pid) {
            return mac_table[i].level;
        }
    }
    return MAC_LEVEL_NONE;
}

// 今後: ポリシー記述・強制・監査API等を拡張
