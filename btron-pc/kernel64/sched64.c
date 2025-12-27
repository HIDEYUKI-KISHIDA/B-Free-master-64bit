// sched64.c - 64ビット用 タイムスライス/プリエンプション雛形（ダミー実装）
#include "../include64/types.h"

static int sched_tick_count = 0;
static int sched_timeslice = 10;

void sched_tick64(void) {
    sched_tick_count++;
    if (sched_tick_count >= sched_timeslice) {
        sched_tick_count = 0;
        // 本物の雰囲気: プロセス切替
        extern void schedule64(void);
        schedule64();
    }
}

void sched_set_timeslice64(int ts) {
    sched_timeslice = ts > 0 ? ts : 10;
}

int sched_get_timeslice64(void) {
    return sched_timeslice;
}
