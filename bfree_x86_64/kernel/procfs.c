#include <string.h>

// 仮想プロセス情報ファイルシステム（/proc）管理雛形

typedef struct procfs_entry {
    const char *path;
    const char *value;
} procfs_entry_t;

#define PROCFS_MAX_ENTRIES 128
static procfs_entry_t procfs_table[PROCFS_MAX_ENTRIES];
static int procfs_count = 0;

// /procノード登録
int procfs_register(const char *path, const char *value) {
    if (procfs_count >= PROCFS_MAX_ENTRIES) return -1;
    procfs_table[procfs_count].path = path;
    procfs_table[procfs_count].value = value;
    procfs_count++;
    return 0;
}

// /procノード検索
const char *procfs_get(const char *path) {
    for (int i = 0; i < procfs_count; ++i) {
        if (strcmp(procfs_table[i].path, path) == 0)
            return procfs_table[i].value;
    }
    return NULL;
}
