#include <stdio.h>
#include "procfs.c"
#include "event.c"

// /proc/event で最新イベントを公開する雛形

static void procfs_event_update(void) {
    static char event_str[64];
    kernel_event_t ev;
    if (event_subscribe(&ev)) {
        snprintf(event_str, sizeof(event_str), "type=%d arg1=%lu arg2=%lu", ev.type, ev.arg1, ev.arg2);
        procfs_register("/proc/event", event_str);
    }
}

// 定期的にprocfs_event_update()を呼ぶことで最新イベントを/proc/eventに反映
