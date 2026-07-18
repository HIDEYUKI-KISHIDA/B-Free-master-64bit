#include <string.h>
#include "device.h"

// 仮想システム情報ファイルシステム（/sys）管理雛形

typedef struct sysfs_entry {
    const char *path;
    const char *value;
} sysfs_entry_t;

#define SYSFS_MAX_ENTRIES 64
static sysfs_entry_t sysfs_table[SYSFS_MAX_ENTRIES];
static int sysfs_count = 0;

// /sysノード登録
int sysfs_register(const char *path, const char *value) {
    if (sysfs_count >= SYSFS_MAX_ENTRIES) return -1;
    sysfs_table[sysfs_count].path = path;
    sysfs_table[sysfs_count].value = value;
    sysfs_count++;
    return 0;
}

// /sysノード検索
const char *sysfs_get(const char *path) {
    for (int i = 0; i < sysfs_count; ++i) {
        if (strcmp(sysfs_table[i].path, path) == 0)
            return sysfs_table[i].value;
    }
    return NULL;
}
