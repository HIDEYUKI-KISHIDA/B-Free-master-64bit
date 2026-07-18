#include <string.h>
#include <stddef.h>
#include "sysfs.c"

// /sys仮想システム属性ノードのユーザー空間APIディスパッチ層雛形

typedef struct {
    const char *name;
    int (*get)(void *buf, size_t len);
    int (*set)(const void *buf, size_t len);
} sysfs_dispatch_entry_t;

#define SYSFS_DISPATCH_MAX 64
static sysfs_dispatch_entry_t sysfs_dispatch_table[SYSFS_DISPATCH_MAX];
static int sysfs_dispatch_count = 0;

// ノード登録
int sysfs_dispatch_register(const char *name, int (*get)(void*,size_t), int (*set)(const void*,size_t)) {
    if (sysfs_dispatch_count >= SYSFS_DISPATCH_MAX) return -1;
    sysfs_dispatch_table[sysfs_dispatch_count].name = name;
    sysfs_dispatch_table[sysfs_dispatch_count].get = get;
    sysfs_dispatch_table[sysfs_dispatch_count].set = set;
    sysfs_dispatch_count++;
    return 0;
}

// get/setディスパッチ
int sysfs_dispatch_get(const char *name, void *buf, size_t len) {
    for (int i = 0; i < sysfs_dispatch_count; ++i) {
        if (strcmp(sysfs_dispatch_table[i].name, name) == 0 && sysfs_dispatch_table[i].get)
            return sysfs_dispatch_table[i].get(buf, len);
    }
    return -1;
}
int sysfs_dispatch_set(const char *name, const void *buf, size_t len) {
    for (int i = 0; i < sysfs_dispatch_count; ++i) {
        if (strcmp(sysfs_dispatch_table[i].name, name) == 0 && sysfs_dispatch_table[i].set)
            return sysfs_dispatch_table[i].set(buf, len);
    }
    return -1;
}
