#include <stdio.h>
#include "procfs.c"
#include "hotplug.c"

// /proc/hotplug でホットプラグイベントを公開する雛形

static void procfs_hotplug_update(hotplug_action_t action, uint32_t device_id) {
    static char hotplug_str[64];
    snprintf(hotplug_str, sizeof(hotplug_str), "action=%d device_id=%u", action, device_id);
    procfs_register("/proc/hotplug", hotplug_str);
}

// ホットプラグ発生時にprocfs_hotplug_update()を呼ぶことで/proc/hotplugに反映
