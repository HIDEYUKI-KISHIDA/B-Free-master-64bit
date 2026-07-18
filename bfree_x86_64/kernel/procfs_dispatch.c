#include <string.h>
#include <stddef.h>
#include "procfs.c"

// /proc仮想ノードのユーザー空間APIディスパッチ層雛形

typedef struct {
    const char *name;
    int (*get)(void *buf, size_t len);
} procfs_dispatch_entry_t;

#define PROCFS_DISPATCH_MAX 64
static procfs_dispatch_entry_t procfs_dispatch_table[PROCFS_DISPATCH_MAX];
static int procfs_dispatch_count = 0;

// ノード登録
int procfs_dispatch_register(const char *name, int (*get)(void*,size_t)) {
    if (procfs_dispatch_count >= PROCFS_DISPATCH_MAX) return -1;
    procfs_dispatch_table[procfs_dispatch_count].name = name;
    procfs_dispatch_table[procfs_dispatch_count].get = get;
    procfs_dispatch_count++;
    return 0;
}

// getディスパッチ
int procfs_dispatch_get(const char *name, void *buf, size_t len) {
    for (int i = 0; i < procfs_dispatch_count; ++i) {
        if (strcmp(procfs_dispatch_table[i].name, name) == 0 && procfs_dispatch_table[i].get)
            return procfs_dispatch_table[i].get(buf, len);
    }
    return -1;
}
