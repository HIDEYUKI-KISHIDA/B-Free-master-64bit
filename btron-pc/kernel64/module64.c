// module64.c - 64ビット用 モジュール/拡張機構雛形（ダミー実装）
#include "../include64/types.h"

#define MAX_MODULE64 8
static const char *module_names[MAX_MODULE64];
static int module_count = 0;

typedef void (*module_init64_t)(void);
static module_init64_t module_inits[MAX_MODULE64];

int module_register64(const char *name, module_init64_t initfunc) {
    if (module_count < MAX_MODULE64) {
        module_names[module_count] = name;
        module_inits[module_count] = initfunc;
        module_count++;
        return 0;
    }
    return -1;
}

int module_init_all64(void) {
    for (int i = 0; i < module_count; i++) {
        if (module_inits[i]) module_inits[i]();
    }
    return 0;
}

const char* module_list64(int idx) {
    if (idx >= 0 && idx < module_count) return module_names[idx];
    return NULL;
}
