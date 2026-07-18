#ifndef SYSCALL_TIME_X86_64_H
#define SYSCALL_TIME_X86_64_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// システムコール番号定義（B-Free独自）
#define BFREE_SYS_CLOCK_GETTIME  29
#define BFREE_SYS_CLOCK_GETRES   30
#define BFREE_SYS_NANOSLEEP      31
#define BFREE_SYS_CLOCK_NANOSLEEP 32

// timespec構造体（POSIX準拠）
struct bfree_timespec {
    long tv_sec;   // 秒
    long tv_nsec;  // ナノ秒
};

// clockid_t型
typedef int clockid_t;

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID  2
#define CLOCK_THREAD_CPUTIME_ID   3

// システムコールハンドラ宣言
long sys_clock_gettime(clockid_t clk_id, struct bfree_timespec *tp);
long sys_clock_getres(clockid_t clk_id, struct bfree_timespec *res);
long sys_nanosleep(const struct bfree_timespec *req, struct bfree_timespec *rem);
long sys_clock_nanosleep(clockid_t clk_id, int flags, 
                         const struct bfree_timespec *req, struct bfree_timespec *rem);

// ハードウェアタイマー関数
void bfree_timer_init(void);
uint64_t bfree_get_time_ns(void);
void bfree_delay_ns(uint64_t ns);

#ifdef __cplusplus
}
#endif

#endif // SYSCALL_TIME_X86_64_H