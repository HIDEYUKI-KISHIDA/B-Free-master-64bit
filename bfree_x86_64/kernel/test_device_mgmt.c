#include <stdio.h>
#include "procfs.c"
#include "event.c"
#include "power.c"
#include "hotplug.c"
#include "devmgmt.c"

int main() {
    // procfs テスト
    procfs_register("/proc/test", "hello");
    printf("procfs: %s\n", procfs_get("/proc/test"));

    // event テスト
    event_publish(EVENT_DEV_HOTPLUG, 1, 1234);
    kernel_event_t ev;
    if (event_subscribe(&ev)) {
        printf("event: type=%d arg1=%lu arg2=%lu\n", ev.type, ev.arg1, ev.arg2);
    }

    // power テスト
    power_set_state(POWER_STATE_SLEEP);
    printf("power: %d\n", power_get_state());

    // hotplug テスト
    hotplug_notify(HOTPLUG_DEVICE_ADD, 5678);
    if (event_subscribe(&ev)) {
        printf("hotplug event: type=%d arg1=%lu arg2=%lu\n", ev.type, ev.arg1, ev.arg2);
    }

    // devmgmt テスト
    devmgmt_register(42, 10, 0);
    devmgmt_set_state(42, DEV_STATE_ERROR);
    dev_state_t st;
    devmgmt_get_state(42, &st);
    printf("devmgmt: state=%d\n", st);

    return 0;
}
