#include <stdint.h>
extern int hpet_available;
/*
 * tsc_calib.c - x86_64 RDTSC周波数キャリブレーション雛形
 * 仕様: TK2_x86_64_Spec.md v2.4準拠
 */
#include <stdint.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// PITを使ったTSC周波数キャリブレーション（雛形）
uint64_t tsc_calibrate_pit(void) {
    // PITで10ms待ち、その間のTSC差分で周波数算出（仮実装）
    // 実際は割り込み禁止・PIT再設定等が必要
    uint64_t tsc_start = rdtsc();
    for (volatile int i = 0; i < 1000000; ++i) ; // 仮ウェイト
    uint64_t tsc_end = rdtsc();
    return (tsc_end - tsc_start) * 100; // 仮: 10ms→1秒換算
}

// HPETを使ったTSC周波数キャリブレーション（雛形）
uint64_t tsc_calibrate_hpet(void) {
    // HPETで10ms待ち、その間のTSC差分で周波数算出（仮実装）
    uint64_t tsc_start = rdtsc();
    for (volatile int i = 0; i < 1000000; ++i) ; // 仮ウェイト
    uint64_t tsc_end = rdtsc();
    return (tsc_end - tsc_start) * 100; // 仮: 10ms→1秒換算
}

// 仕様書: RDTSC使用時は必ず初回キャリブレーション
uint64_t tsc_calibrate(void) {
    // HPET優先、なければPIT
    extern int hpet_available;
    if (hpet_available) {
        return tsc_calibrate_hpet();
    } else {
        return tsc_calibrate_pit();
    }
}
