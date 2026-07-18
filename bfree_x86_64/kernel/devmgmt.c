#include <stdint.h>
#include <stddef.h>

// 高度なデバイス管理API雛形

typedef enum {
    DEV_STATE_OK,
    DEV_STATE_ERROR,
    DEV_STATE_SUSPEND,
    DEV_STATE_REMOVED
} dev_state_t;

typedef struct {
    uint32_t device_id;
    dev_state_t state;
    int priority;
    int dependency;
    // ...拡張用...
} devmgmt_info_t;

#define DEVMGMT_MAX_DEVICES 128
static devmgmt_info_t devmgmt_table[DEVMGMT_MAX_DEVICES];
static int devmgmt_count = 0;

// デバイス管理情報登録
int devmgmt_register(uint32_t device_id, int priority, int dependency) {
    if (devmgmt_count >= DEVMGMT_MAX_DEVICES) return -1;
    devmgmt_table[devmgmt_count].device_id = device_id;
    devmgmt_table[devmgmt_count].state = DEV_STATE_OK;
    devmgmt_table[devmgmt_count].priority = priority;
    devmgmt_table[devmgmt_count].dependency = dependency;
    devmgmt_count++;
    return 0;
}

// デバイス状態変更
int devmgmt_set_state(uint32_t device_id, dev_state_t state) {
    for (int i = 0; i < devmgmt_count; ++i) {
        if (devmgmt_table[i].device_id == device_id) {
            devmgmt_table[i].state = state;
            return 0;
        }
    }
    return -1;
}

// デバイス状態取得
int devmgmt_get_state(uint32_t device_id, dev_state_t *out) {
    for (int i = 0; i < devmgmt_count; ++i) {
        if (devmgmt_table[i].device_id == device_id) {
            *out = devmgmt_table[i].state;
            return 0;
        }
    }
    return -1;
}
