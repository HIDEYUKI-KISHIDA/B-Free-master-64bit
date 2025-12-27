// kernel_arm/process_arm.c
// ARM プロセス/スレッド管理雛形
// 2025/12/27 新規作成

#include "../btron-pc/include_arm/types_arm.h"
#include <stdint.h>

#define MAX_PROCESSES 32
#define MAX_THREADS   64

typedef struct {
    int pid;
    int state;
    void *stack_ptr;
    void (*entry)(void);
    int parent_pid;
    int priority;
    int fd_table[8];
    // ...他に必要な情報
} process_t;

typedef struct {
    int tid;
    int state;
    void *stack_ptr;
    void (*entry)(void);
    int parent_pid;
    int priority;
    // ...他に必要な情報
} thread_t;

static process_t process_table[MAX_PROCESSES];
static thread_t thread_table[MAX_THREADS];

// プロセス初期化
void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_table[i].pid = -1;
        process_table[i].state = 0;
        process_table[i].stack_ptr = NULL;
        process_table[i].entry = NULL;
        process_table[i].parent_pid = -1;
        process_table[i].priority = 0;
        for(int j=0;j<8;++j) process_table[i].fd_table[j] = -1;
    }
    for (int i = 0; i < MAX_THREADS; ++i) {
        thread_table[i].tid = -1;
        thread_table[i].state = 0;
        thread_table[i].stack_ptr = NULL;
        thread_table[i].entry = NULL;
        thread_table[i].parent_pid = -1;
        thread_table[i].priority = 0;
    }
}

// プロセス生成
int process_create(void (*entry)(void)) {
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].pid == -1) {
            process_table[i].pid = i+1;
            process_table[i].state = 1; // RUNNABLE
            process_table[i].stack_ptr = NULL; // 仮: alloc_page等で割当て推奨
            process_table[i].entry = entry;
            process_table[i].parent_pid = 0; // 仮: 親なし
            process_table[i].priority = 0;
            for(int j=0;j<8;++j) process_table[i].fd_table[j] = -1;
            return process_table[i].pid;
        }
    }
    return -1;
}

// スレッド生成
int thread_create(int pid, void (*entry)(void)) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (thread_table[i].tid == -1) {
            thread_table[i].tid = i+1;
            thread_table[i].state = 1; // RUNNABLE
            thread_table[i].stack_ptr = NULL; // 仮: alloc_page等で割当て推奨
            thread_table[i].entry = entry;
            thread_table[i].parent_pid = pid;
            thread_table[i].priority = 0;
            return thread_table[i].tid;
        }
    }
    return -1;
}

// スケジューラ
void schedule(void) {
    // シンプルなラウンドロビン・優先度順スケジューリング雛形
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        if (process_table[i].pid != -1 && process_table[i].state == 1) {
            process_table[i].state = 2; // RUNNING
            if(process_table[i].entry) process_table[i].entry();
            process_table[i].state = 1; // RUNNABLEに戻す（仮）
        }
    }
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (thread_table[i].tid != -1 && thread_table[i].state == 1) {
            thread_table[i].state = 2; // RUNNING
            if(thread_table[i].entry) thread_table[i].entry();
            thread_table[i].state = 1;
        }
    }
    // 状態: 0=未使用, 1=RUNNABLE, 2=RUNNING, 3=WAITING, 4=ZOMBIE
}
