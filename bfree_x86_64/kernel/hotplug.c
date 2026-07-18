// --- TK2独自ホットプラグ/IPCイベント拡張 ---
void hotplug_notify_tk2_ipc(const char* channel, uint32_t device_id) {
    extern void event_publish_tk2_ipc(const char* channel, uintptr_t arg);
    event_publish_tk2_ipc(channel, device_id);
}
#include <stdint.h>
#include "event.c"

// ホットプラグ管理API雛形

typedef enum {
    HOTPLUG_DEVICE_ADD,
    HOTPLUG_DEVICE_REMOVE
} hotplug_action_t;

// デバイスホットプラグ通知
void hotplug_notify(hotplug_action_t action, uint32_t device_id) {
    event_publish(EVENT_DEV_HOTPLUG, action, device_id);
    // evdevノードの動的追加/削除
    extern void devfs_add_evdev_node(uint32_t device_id);
    extern void devfs_remove_evdev_node(uint32_t device_id);
    if (action == HOTPLUG_DEVICE_ADD) {
        devfs_add_evdev_node(device_id);
    } else if (action == HOTPLUG_DEVICE_REMOVE) {
        devfs_remove_evdev_node(device_id);
    }
}

// デバイス検出・管理ロジック例（実際はPCI/USB/ACPI等と連携）
void detect_input_devices(void) {
    // 例: 新しい入力デバイスを検出したら
    uint32_t new_dev_id = 100; // 仮
    hotplug_notify(HOTPLUG_DEVICE_ADD, new_dev_id);
}
