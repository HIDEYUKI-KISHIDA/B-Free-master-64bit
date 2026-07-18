#include <stdio.h>
#include "event.c"

// デバイスイベントの履歴・監査・トレース機能雛形

typedef struct {
    kernel_event_t ev;
} eventlog_entry_t;

#define EVENTLOG_MAX 128
static eventlog_entry_t eventlog_table[EVENTLOG_MAX];
static int eventlog_count = 0;

// イベント記録
void eventlog_record(kernel_event_t *ev) {
    if (eventlog_count < EVENTLOG_MAX) {
        eventlog_table[eventlog_count++].ev = *ev;
    }
}

// イベント履歴出力
void eventlog_dump(void) {
    for (int i = 0; i < eventlog_count; ++i) {
        printf("event[%d]: type=%d arg1=%lu arg2=%lu\n", i, eventlog_table[i].ev.type, eventlog_table[i].ev.arg1, eventlog_table[i].ev.arg2);
    }
}

// 今後: /proc/eventlog等で公開、監査・トレース拡張
