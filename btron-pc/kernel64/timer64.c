// timer64.c - 64ビット用 タイマー割り込み雛形
#include "../include64/types.h"

static u64 timer_ticks = 0;

void timer_handler64(void) {
    timer_ticks++;
    // TODO: プロセス切り替え等
}

void timer_init64(void) {
    // TODO: PIT/APIC等のタイマー初期化
}

u64 get_timer_ticks64(void) {
    return timer_ticks;
}

// --- 追加: 32ビット版timer.cの雰囲気を再現したAPI群（ダミー実装） ---

typedef void (*timer_callback64_t)(void *);

typedef struct timer_entry64 {
    struct timer_entry64 *next;
    u64 time;
    timer_callback64_t func;
    void *argp;
} timer_entry64_t;

#define MAX_TIMER64 16
static timer_entry64_t timer_list64[MAX_TIMER64];
static timer_entry64_t *free_timer64 = NULL;
static timer_entry64_t *active_timer64 = NULL;

void init_timer64(void) {
    // タイマーリスト初期化（ダミー）
    for (int i = 0; i < MAX_TIMER64 - 1; i++) {
        timer_list64[i].next = &timer_list64[i + 1];
    }
    timer_list64[MAX_TIMER64 - 1].next = NULL;
    free_timer64 = timer_list64;
    active_timer64 = NULL;
}

void start_interval64(void) {
    // 割り込みタイマー開始（ダミー）
    // 本来はPIT/APIC等の初期化
}

void intr_interval64(void) {
    // 割り込み発生時の処理（ダミー）
    // 本来はtick進行やコールバック呼び出し
    timer_ticks++;
}

void set_timer64(u64 time, timer_callback64_t func, void *argp) {
    // タイマー登録（ダミー）
    if (!free_timer64 || !func || time == 0) return;
    timer_entry64_t *entry = free_timer64;
    free_timer64 = free_timer64->next;
    entry->time = time;
    entry->func = func;
    entry->argp = argp;
    entry->next = active_timer64;
    active_timer64 = entry;
    // 本来はソート挿入や割り込み設定
}

// --- ここまで追加 ---
