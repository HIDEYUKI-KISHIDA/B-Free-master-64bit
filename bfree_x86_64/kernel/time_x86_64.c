// time_x86_64.c - x86_64用ハードウェアタイマー・時刻管理
// Wayland動作に必要な時刻・タイマー機能を提供

#include "syscall_time_x86_64.h"
#include <stdint.h>
#include <stdbool.h>

// HPET (High Precision Event Timer) レジスタ定義
#define HPET_BASE_ADDRESS  0xFED00000UL
#define HPET_MAIN_COUNTER  0xF0  // メインカウンタレジスタ（64ビット）
#define HPET_CONFIG        0x00  // 設定レジスタ

// PIT (Programmable Interval Timer) レジスタ定義
#define PIT_DATA_PORT      0x40
#define PIT_COMMAND_PORT   0x43
#define PIT_COUNTER_0      0x00
#define PIT_MODE_SQUARE    0x36  // 方形波モード
#define PIT_MODE_RATE      0x34  // レートジェネレータモード

// TSC (Time Stamp Counter) 利用可否
static bool tsc_available = false;
static bool hpet_available = false;
static bool pit_available = false;

// 起動時カウンタ値
static uint64_t boot_hpet_ticks = 0;
static uint64_t boot_tsc_ticks = 0;

// HPET周波数（10MHz = 100ns/tick が一般的）

// TSC周波数（後で検出）
static uint64_t tsc_freq_hz = 0;

/* Kernel build has no stdio; silence debug prints. */
#define printf(...) ((void)0)

// PITは1.193182MHz
#define PIT_FREQ_HZ 1193182ULL

// I/Oポートアクセス用インライン関数
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "d"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "d"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "d"(port));
}

// MMIOアクセス用
static inline uint32_t mmio_read32(uint64_t addr) {
    return *((volatile uint32_t*)addr);
}

static inline uint64_t mmio_read64(uint64_t addr) {
    return *((volatile uint64_t*)addr);
}

// RDTSC命令でTSCを取得
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// TSCが安定しているかチェック（CPUIDで確認）
static bool check_tsc_stable(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // CPUID.80000007H:EDX[8] = invariant TSC
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                      : "a"(0x80000007));
    return (edx & (1 << 8)) != 0;
}

// HPETの検出と初期化
static bool hpet_init(void) {
    // HPETが存在するかチェック
    // ACPIテーブルからHPET情報を取得する必要があるが、
    // ここでは簡易的に固定アドレスを試し読み
    
    uint64_t hpet_val = mmio_read64(HPET_BASE_ADDRESS + HPET_MAIN_COUNTER);
    
    // 0でない値が読めればHPET存在とみなす（簡易チェック）
    if (hpet_val != 0) {
        hpet_available = true;
        boot_hpet_ticks = hpet_val;
        printf("[TIME] HPET initialized at 0x%lX, initial value: %lu\n", 
               HPET_BASE_ADDRESS, boot_hpet_ticks);
        return true;
    }
    
    printf("[TIME] HPET not available\n");
    return false;
}

// PITの初期化
static bool pit_init(void) {
    // PITは常に存在すると仮定
    pit_available = true;
    printf("[TIME] PIT initialized (1.193182 MHz)\n");
    return true;
}

// TSCの初期化
static bool tsc_init(void) {
    if (check_tsc_stable()) {
        tsc_available = true;
        boot_tsc_ticks = rdtsc();
        // TSC周波数は後でcalibrate
        printf("[TIME] TSC initialized (invariant)\n");
        return true;
    }
    printf("[TIME] TSC not stable\n");
    return false;
}

// 全体初期化
void bfree_timer_init(void) {
    printf("[TIME] Initializing timer subsystem...\n");
    
    // 1. HPETを試す（最も高精度）
    if (hpet_init()) {
        return;
    }
    
    // 2. TSCを試す（高速だがCPU依存）
    if (tsc_init()) {
        return;
    }
    
    // 3. PITにフォールバック（低精度だが確実）
    pit_init();
}

// 現在時刻をナノ秒で取得（起動時からの経過時間）
uint64_t bfree_get_time_ns(void) {
    if (hpet_available) {
        uint64_t current = mmio_read64(HPET_BASE_ADDRESS + HPET_MAIN_COUNTER);
        uint64_t elapsed_ticks = current - boot_hpet_ticks;
        // HPETは10MHz (100ns/tick) が一般的
        return elapsed_ticks * 100ULL;
    }
    
    if (tsc_available) {
        uint64_t current = rdtsc();
        uint64_t elapsed_ticks = current - boot_tsc_ticks;
        // TSC周波数が不明なので、仮に2GHzと仮定
        // 実際にはcalibrateが必要
        if (tsc_freq_hz > 0) {
            return (elapsed_ticks * 1000000000ULL) / tsc_freq_hz;
        }
        // 仮の周波数（2GHz）
        return (elapsed_ticks * 1000000000ULL) / 2000000000ULL;
    }
    
    if (pit_available) {
        // PITは精度が低い（約838ns/tick）
        // 実際のPIT読み取りは複雑なので、簡易実装
        return 0; // TODO: PIT実装
    }
    
    return 0;
}

// 指定ナノ秒だけ待機
void bfree_delay_ns(uint64_t ns) {
    uint64_t start = bfree_get_time_ns();
    while ((bfree_get_time_ns() - start) < ns) {
        // ビジーウェイト
        __asm__ volatile ("pause" ::: "memory");
    }
}

// ============================================================
// システムコールハンドラ実装
// ============================================================

long bfree_time_sys_clock_gettime(clockid_t clk_id, struct bfree_timespec *tp) {
    if (tp == 0) {
        return -1; // EFAULT
    }
    
    uint64_t ns = bfree_get_time_ns();
    
    switch (clk_id) {
        case CLOCK_MONOTONIC:
            // 起動時からの経過時間
            tp->tv_sec = ns / 1000000000ULL;
            tp->tv_nsec = ns % 1000000000ULL;
            return 0;
            
        case CLOCK_REALTIME:
            // 現在は固定エポック（1970-01-01 00:00:00 UTC）
            // 将来的にRTC/NTPと同期
            tp->tv_sec = 0;
            tp->tv_nsec = 0;
            return 0;
            
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
            // CPU時間は未実装
            tp->tv_sec = 0;
            tp->tv_nsec = 0;
            return 0;
            
        default:
            return -1; // EINVAL
    }
}

long bfree_time_sys_clock_getres(clockid_t clk_id, struct bfree_timespec *res) {
    if (res == 0) {
        return -1; // EFAULT
    }
    
    switch (clk_id) {
        case CLOCK_MONOTONIC:
        case CLOCK_REALTIME:
            if (hpet_available) {
                // HPET: 100ns分解能
                res->tv_sec = 0;
                res->tv_nsec = 100;
            } else if (tsc_available) {
                // TSC: CPU周波数に依存（例: 2GHzなら0.5ns）
                res->tv_sec = 0;
                res->tv_nsec = 1; // 1ns（実際はもっと粗い）
            } else {
                // PIT: 約838ns
                res->tv_sec = 0;
                res->tv_nsec = 1000; // 1μs
            }
            return 0;
            
        default:
            return -1; // EINVAL
    }
}

long bfree_time_sys_nanosleep(const struct bfree_timespec *req, struct bfree_timespec *rem) {
    if (req == 0) {
        return -1; // EFAULT
    }
    
    uint64_t total_ns = (uint64_t)req->tv_sec * 1000000000ULL + req->tv_nsec;
    
    bfree_delay_ns(total_ns);
    
    if (rem != 0) {
        // 割り込まれなければ残り時間は0
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    
    return 0;
}

long bfree_time_sys_clock_nanosleep(clockid_t clk_id, int flags, 
                         const struct bfree_timespec *req, 
                         struct bfree_timespec *rem) {
    if (req == 0) {
        return -1; // EFAULT
    }
    
    // flags: 0 = 相対時刻, TIMER_ABSTIME = 絶対時刻
    // 今は相対時刻のみサポート
    if (flags != 0) {
        // 絶対時刻は未実装
        return -1; // EINVAL
    }
    
    // CLOCK_MONOTONIC または CLOCK_REALTIME のみサポート
    if (clk_id != CLOCK_MONOTONIC && clk_id != CLOCK_REALTIME) {
        return -1; // EINVAL
    }
    
    return bfree_time_sys_nanosleep(req, rem);
}