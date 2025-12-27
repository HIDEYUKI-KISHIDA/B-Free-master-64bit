// conf64.c - 64ビット用 シンプルな設定管理雛形（ダミー実装）
#include "../include64/types.h"

#define MAX_CONF64 8
static char conf_keys[MAX_CONF64][32];
static char conf_values[MAX_CONF64][64];
static int conf_count = 0;

int conf_set64(const char *key, const char *value) {
    for (int i = 0; i < conf_count; i++) {
        if (!strcmp(conf_keys[i], key)) {
            strncpy(conf_values[i], value, 63); conf_values[i][63]=0;
            return 0;
        }
    }
    if (conf_count < MAX_CONF64) {
        strncpy(conf_keys[conf_count], key, 31); conf_keys[conf_count][31]=0;
        strncpy(conf_values[conf_count], value, 63); conf_values[conf_count][63]=0;
        conf_count++;
        return 0;
    }
    return -1;
}

const char* conf_get64(const char *key) {
    for (int i = 0; i < conf_count; i++) {
        if (!strcmp(conf_keys[i], key)) return conf_values[i];
    }
    return NULL;
}

void conf_load64(void) {
    // 本来はファイルから設定を読む
    conf_set64("shell.prompt", "bfree64>");
    conf_set64("mem.limit", "64MB");
}
