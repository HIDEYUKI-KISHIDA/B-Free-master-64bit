// --- TK2独自イベント購読API拡張 ---
int event_user_subscribe_tk2_ipc(kernel_event_t *out, const char* channel) {
    kernel_event_t ev;
    while (event_subscribe(&ev)) {
        if (ev.type == 100 && (const char*)ev.arg1 && strcmp((const char*)ev.arg1, channel) == 0) {
            *out = ev;
            return 1;
        }
    }
    return 0;
}

int event_user_subscribe_tk2_system(kernel_event_t *out, uintptr_t code) {
    kernel_event_t ev;
    while (event_subscribe(&ev)) {
        if (ev.type == 101 && ev.arg1 == code) {
            *out = ev;
            return 1;
        }
    }
    return 0;
}
#include "event.c"

// ユーザー空間向けイベント通知API雛形

// ユーザー空間プロセスがイベントを購読するためのAPI（例: ポーリング）
// type/device_idでフィルタ可能な拡張
int event_user_subscribe_filtered(kernel_event_t *out, event_type_t type_filter, uint32_t device_id_filter) {
    kernel_event_t ev;
    while (event_subscribe(&ev)) {
        if ((type_filter == (event_type_t)-1 || ev.type == type_filter) &&
            (device_id_filter == (uint32_t)-1 || ev.arg2 == device_id_filter)) {
            *out = ev;
            return 1;
        }
    }
    return 0;
}

// 従来の全イベント購読も残す
int event_user_subscribe(kernel_event_t *out) {
    return event_subscribe(out);
}
