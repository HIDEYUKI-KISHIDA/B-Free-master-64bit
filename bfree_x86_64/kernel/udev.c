#include <stdint.h>
#include <stdio.h>
#include "event.c"
#include "hotplug.c"

// ユーザー空間通知・自動設定（udev的仕組み）雛形

// ユーザー空間へのホットプラグ通知（例: ログ出力）
void udev_notify(hotplug_action_t action, uint32_t device_id) {
    printf("udev: action=%d device_id=%u\n", action, device_id);
    // 今後: ユーザー空間プロセスへのIPC/イベント配信等
}

// ホットプラグ発生時にudev_notifyを呼ぶことでユーザー空間に通知
