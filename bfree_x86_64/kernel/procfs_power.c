#include <stdio.h>
#include "procfs.c"
#include "power.c"

// /proc/power で電源状態を公開する雛形

static void procfs_power_update(void) {
    static char power_str[32];
    snprintf(power_str, sizeof(power_str), "%d", power_get_state());
    procfs_register("/proc/power", power_str);
}

// 定期的にprocfs_power_update()を呼ぶことで最新電源状態を/proc/powerに反映
