#include "interrupt.h"
#include <stdint.h>

// 64bit時刻型（TK2流に合わせて型名はLSYSTIMとする）
typedef uint64_t LSYSTIM;


// 現在時刻（システム起動からの経過マイクロ秒）
volatile LSYSTIM current_time = 0;

// タイマイベント（最大32件同時管理）
#define MAX_TIMER_EVENTS 32
typedef void (*timer_callback_t)(void *);
typedef struct {
    LSYSTIM expire_time;
    timer_callback_t callback;
    void *arg;
    int active;
} timer_event_t;

static timer_event_t timer_events[MAX_TIMER_EVENTS] = {0};

// 割り込み禁止でcurrent_timeを安全に取得
LSYSTIM knl_get_current_time(void) {
    uint64_t f = save_and_disable_interrupts();
    LSYSTIM t = current_time;
    restore_interrupts(f);
    return t;
}

// タイマイベント登録（絶対時刻指定、最大32件）
// 戻り値: 登録成功したイベントID、失敗時-1
extern void uart_puts(const char*);
extern void uart_puthex64(uint64_t val);
int timer_set_event(LSYSTIM expire_time, timer_callback_t callback, void *arg) {
    int ret = -1;
    BEGIN_CRITICAL_SECTION
    for (int i = 0; i < MAX_TIMER_EVENTS; ++i) {
        if (!timer_events[i].active) {
            timer_events[i].expire_time = expire_time;
            timer_events[i].callback = callback;
            timer_events[i].arg = arg;
            timer_events[i].active = 1;
            // デバッグ出力
            char buf[64];
            uart_puts("[TIMER] Set id=");
            buf[0] = '0' + (i / 10); buf[1] = '0' + (i % 10); buf[2] = 0;
            uart_puts(buf);
            uart_puts(" expire=0x");
            for (int j = 15; j >= 0; --j) {
                int d = (expire_time >> (j*4)) & 0xF;
                uart_puts((char[]){(char)(d<10?'0'+d:'A'+d-10),0});
            }
            uart_puts("\n");
            ret = i;
            break;
        }
    }
    if (ret == -1) {
        uart_puts("[TIMER] Set failed (full)\n");
    }
    END_CRITICAL_SECTION
    return ret;
}

// タイマイベント解除
void timer_cancel_event(int id) {
    if (id < 0 || id >= MAX_TIMER_EVENTS) return;
    BEGIN_CRITICAL_SECTION
    timer_events[id].active = 0;
    timer_events[id].callback = 0;
    timer_events[id].arg = 0;
    uart_puts("[TIMER] Cancel id=");
    char buf[3]; buf[0]='0'+(id/10); buf[1]='0'+(id%10); buf[2]=0;
    uart_puts(buf); uart_puts("\n");
    END_CRITICAL_SECTION
}

/* Drop boot/init timerfd callbacks before desktop.elf (stale fn ptrs → #GP in IRQ). */
void timer_purge_all(void)
{
    BEGIN_CRITICAL_SECTION
    for (int i = 0; i < MAX_TIMER_EVENTS; ++i) {
        timer_events[i].active = 0;
        timer_events[i].callback = 0;
        timer_events[i].arg = 0;
        timer_events[i].expire_time = 0;
    }
    END_CRITICAL_SECTION
    uart_puts("[TIMER] purge all\n");
}

static int timer_callback_is_kernel(timer_callback_t cb)
{
    uintptr_t u = (uintptr_t)(void *)cb;
    /*
     * kernel.elf is linked at 0x100000; ring3 init.elf starts at 0x400000.
     * Do NOT treat 0x100000..0x03C00000 as kernel — desktop.elf @ 0x2800000
     * falls in that window and stale callbacks there were invoked from IRQ (CS=8).
     */
    return u >= 0x100000ULL && u < 0x400000ULL;
}

// タイマ割り込みハンドラ（LAPIC等から呼ぶ）
// デバッグ用: 100回に1回だけシリアルに出力
extern void syslog(int priority, const char *format, ...);

void timer_handler(void) {
    static int tick_count = 0;
    BEGIN_CRITICAL_SECTION
    current_time += 10000; // 10ms = 10000us
    ++tick_count; // syslogは出力しない（シリアルが埋まるため）
    for (int i = 0; i < MAX_TIMER_EVENTS; ++i) {
        if (timer_events[i].active && timer_events[i].expire_time > 0 &&
            current_time >= timer_events[i].expire_time) {
            timer_events[i].active = 0;
            uart_puts("[TIMER] Fire id=");
            char buf[3]; buf[0]='0'+(i/10); buf[1]='0'+(i%10); buf[2]=0;
            uart_puts(buf);
            uart_puts(" now=0x");
            for (int j = 15; j >= 0; --j) {
                int d = (current_time >> (j*4)) & 0xF;
                uart_puts((char[]){(char)(d<10?'0'+d:'A'+d-10),0});
            }
            uart_puts("\n");
            if (timer_events[i].callback) {
                if (timer_callback_is_kernel(timer_events[i].callback)) {
                    timer_events[i].callback(timer_events[i].arg);
                } else {
                    uart_puts("[TIMER] skip bad callback id=");
                    char buf[3];
                    buf[0] = '0' + (char)(i / 10);
                    buf[1] = '0' + (char)(i % 10);
                    buf[2] = 0;
                    uart_puts(buf);
                    uart_puts(" fn=");
                    uart_puthex64((uint64_t)(uintptr_t)timer_events[i].callback);
                    uart_puts("\n");
                }
            }
        }
    }
    END_CRITICAL_SECTION
}
