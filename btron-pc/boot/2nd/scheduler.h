#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include "types.h"
#include "process.h"

/* スケジューラアルゴリズム */
#define SCHED_FIFO      0       /* First In First Out */
#define SCHED_RR        1       /* Round Robin */
#define SCHED_PRIORITY  2       /* Priority-based */

/* デフォルトスケジューラ */
#define DEFAULT_SCHEDULER  SCHED_PRIORITY

/* スケジューラ統計情報 */
struct sched_stats {
    int total_ticks;            /* 総スケジューリング回数 */
    int context_switches;       /* コンテキストスイッチ回数 */
    UWORD64 idle_ticks;         /* アイドル時間 */
    int ready_queue_size;       /* レディキューのサイズ */
};

/* レディキュー */
struct ready_queue {
    int pids[MAX_PROCESS];      /* プロセスID配列 */
    int count;                  /* キュー内のプロセス数 */
};

/* グローバルスケジューラ変数 */
extern int current_scheduler;
extern struct sched_stats sched_stats;
extern struct ready_queue ready_queue;
extern UWORD64 timer_ticks;

/* スケジューラ関数 */
int scheduler_init(int algo);
void scheduler(void);
int add_to_ready_queue(int pid);
int remove_from_ready_queue(int pid);
int is_in_ready_queue(int pid);
int get_next_process(void);

/* スケジューラアルゴリズム */
int select_process_fifo(void);
int select_process_rr(void);
int select_process_priority(void);

/* タイマーハンドラ */
void timer_tick(void);

/* スケジューラ統計 */
void sched_stats_show(void);

#endif  /* __SCHEDULER_H__ */
