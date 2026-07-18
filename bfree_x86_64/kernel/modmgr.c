#include <stdint.h>
#include <stddef.h>

// デバイス・ドライバのロード/アンロード（モジュール化、動的リンク）雛形

typedef struct module {
    const char *name;
    int loaded;
    // ...拡張用...
} module_t;

#define MODMGR_MAX_MODULES 64
static module_t modmgr_table[MODMGR_MAX_MODULES];
static int modmgr_count = 0;

// モジュール登録
int modmgr_register(const char *name) {
    if (modmgr_count >= MODMGR_MAX_MODULES) return -1;
    modmgr_table[modmgr_count].name = name;
    modmgr_table[modmgr_count].loaded = 0;
    modmgr_count++;
    return 0;
}

// ロード/アンロード
int modmgr_load(const char *name) {
    for (int i = 0; i < modmgr_count; ++i) {
        if (strcmp(modmgr_table[i].name, name) == 0) {
            modmgr_table[i].loaded = 1;
            return 0;
        }
    }
    return -1;
}
int modmgr_unload(const char *name) {
    for (int i = 0; i < modmgr_count; ++i) {
        if (strcmp(modmgr_table[i].name, name) == 0) {
            modmgr_table[i].loaded = 0;
            return 0;
        }
    }
    return -1;
}
