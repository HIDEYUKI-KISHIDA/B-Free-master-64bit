// rtc64.c - 64ビット用 ローカルタイマー/RTC雛形（ダミー実装）
#include "../include64/types.h"

static unsigned long rtc_time = 1700000000; // 仮のUNIX時刻

unsigned long rtc_gettime64(void) {
    return rtc_time;
}

void rtc_settime64(unsigned long t) {
    rtc_time = t;
}

void rtc_tick64(void) {
    rtc_time++;
}
