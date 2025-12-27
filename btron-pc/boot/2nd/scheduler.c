#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
#include "scheduler.h"
#include "lib.h"

/* グローバルスケジューラ変数 */
int current_scheduler = SCHED_PRIORITY;
struct sched_stats sched_stats = {0};
struct ready_queue ready_queue = {0};
UWORD64 timer_ticks = 0;

/* スケジューラ初期化 */
int scheduler_init(int algo)
{
    int i;
    
    if (algo == SCHED_FIFO || algo == SCHED_RR || algo == SCHED_PRIORITY) {
        current_scheduler = algo;
    } else {
        current_scheduler = DEFAULT_SCHEDULER;
    }
    
    /* レディキュー初期化 */
    ready_queue.count = 0;
    for (i = 0; i < MAX_PROCESS; i++) {
        ready_queue.pids[i] = -1;
    }
    
    /* スケジューラ統計初期化 */
    sched_stats.total_ticks = 0;
    sched_stats.context_switches = 0;
    sched_stats.idle_ticks = 0;
    sched_stats.ready_queue_size = 0;
    
    console_printf("Scheduler initialized (algo=%d)\n", current_scheduler);
    
    return 0;
}

/* レディキューにプロセスを追加 */
int add_to_ready_queue(int pid)
{
    struct process *proc;
    
    if (is_in_ready_queue(pid)) {
        return -1;  /* 既に登録済み */
    }
    
    proc = get_process(pid);
    if (!proc || proc->state == PROC_FREE) {
        return -1;
    }
    
    /* キューに追加 */
    if (ready_queue.count >= MAX_PROCESS) {
        return -1;  /* キュー満杯 */
    }
    
    ready_queue.pids[ready_queue.count] = pid;
    ready_queue.count++;
    
    sched_stats.ready_queue_size = ready_queue.count;
    
    return 0;
}

/* レディキューからプロセスを削除 */
int remove_from_ready_queue(int pid)
{
    int i, j;
    
    for (i = 0; i < ready_queue.count; i++) {
        if (ready_queue.pids[i] == pid) {
            /* 見つかった。後ろのプロセスを前に詰める */
            for (j = i; j < ready_queue.count - 1; j++) {
                ready_queue.pids[j] = ready_queue.pids[j + 1];
            }
            ready_queue.pids[ready_queue.count - 1] = -1;
            ready_queue.count--;
            
            sched_stats.ready_queue_size = ready_queue.count;
            
            return 0;
        }
    }
    
    return -1;  /* 見つからない */
}

/* レディキューに存在するか確認 */
int is_in_ready_queue(int pid)
{
    int i;
    
    for (i = 0; i < ready_queue.count; i++) {
        if (ready_queue.pids[i] == pid) {
            return 1;
        }
    }
    
    return 0;
}

/* FIFO（先入先出）方式で次のプロセスを選択 */
int select_process_fifo(void)
{
    if (ready_queue.count == 0) {
        return -1;
    }
    
    return ready_queue.pids[0];
}

/* Round Robin（ラウンドロビン）方式で次のプロセスを選択 */
int select_process_rr(void)
{
    static int last_selected = 0;
    int next_idx;
    
    if (ready_queue.count == 0) {
        return -1;
    }
    
    /* 次のプロセスを選択（循環） */
    next_idx = (last_selected + 1) % ready_queue.count;
    last_selected = next_idx;
    
    return ready_queue.pids[next_idx];
}

/* 優先度ベースで次のプロセスを選択 */
int select_process_priority(void)
{
    int i;
    int selected_pid = -1;
    int highest_priority = PRIO_MAX + 1;
    struct process *proc;
    
    if (ready_queue.count == 0) {
        return -1;
    }
    
    /* 最も高い優先度（数値が小さい）のプロセスを探す */
    for (i = 0; i < ready_queue.count; i++) {
        proc = get_process(ready_queue.pids[i]);
        if (proc && proc->priority < highest_priority) {
            highest_priority = proc->priority;
            selected_pid = ready_queue.pids[i];
        }
    }
    
    return selected_pid;
}

/* 次のプロセスを取得 */
int get_next_process(void)
{
    switch (current_scheduler) {
        case SCHED_FIFO:
            return select_process_fifo();
        case SCHED_RR:
            return select_process_rr();
        case SCHED_PRIORITY:
        default:
            return select_process_priority();
    }
}

/* スケジューラメイン */
void scheduler(void)
{
    int next_pid;
    struct process *next_proc;
    struct process *old_proc = current_proc;
    
    sched_stats.total_ticks++;
    
    /* 次のプロセスを選択 */
    next_pid = get_next_process();
    
    if (next_pid == -1) {
        /* アイドル状態 */
        sched_stats.idle_ticks++;
        return;
    }
    
    next_proc = get_process(next_pid);
    if (!next_proc || next_proc->state != PROC_READY) {
        return;
    }
    
    /* コンテキストスイッチが必要か確認 */
    if (current_proc && current_proc->pid == next_pid) {
        /* 同じプロセスを続行 */
        current_proc->ticks--;
        return;
    }
    
    /* 現在のプロセスをレディキューに戻す */
    if (old_proc && old_proc->state == PROC_RUNNING) {
        old_proc->state = PROC_READY;
        old_proc->ticks = TIME_SLICE;
    }
    
    /* 新しいプロセスに切り替え */
    switch_to_process(next_proc);
    sched_stats.context_switches++;
    
    console_printf("Context switch: %d -> %d\n", 
                   old_proc ? old_proc->pid : -1, next_pid);
}

/* タイマー割り込みハンドラ */
void timer_tick(void)
{
    timer_ticks++;
    
    if (!current_proc) {
        return;
    }
    
    /* 現在のプロセスのタイムスライスをデクリメント */
    current_proc->ticks--;
    
    /* タイムスライス満了の場合はスケジューラを呼ぶ */
    if (current_proc->ticks <= 0) {
        scheduler();
    }
}

/* スケジューラ統計情報を表示 */
void sched_stats_show(void)
{
    console_printf("=== Scheduler Statistics ===\n");
    console_printf("Total ticks:        %d\n", sched_stats.total_ticks);
    console_printf("Context switches:   %d\n", sched_stats.context_switches);
    console_printf("Idle ticks:         %ld\n", sched_stats.idle_ticks);
    console_printf("Ready queue size:   %d\n", sched_stats.ready_queue_size);
    console_printf("Timer ticks:        %ld\n", timer_ticks);
    
    if (current_proc) {
        console_printf("Current process:    PID=%d, Ticks remaining=%d\n",
                      current_proc->pid, current_proc->ticks);
    }
}

/* レディキューの内容を表示（デバッグ用） */
void ready_queue_show(void)
{
    int i;
    struct process *proc;
    
    console_printf("=== Ready Queue (size=%d) ===\n", ready_queue.count);
    
    for (i = 0; i < ready_queue.count; i++) {
        proc = get_process(ready_queue.pids[i]);
        if (proc) {
            console_printf("[%d] PID=%d, Priority=%d, State=%d\n",
                          i, proc->pid, proc->priority, proc->state);
        }
    }
}
