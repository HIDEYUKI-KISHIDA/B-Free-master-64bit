// thread64.c - 64ビット用 スレッド雛形（ダミー実装）
#include "../include64/types.h"

typedef unsigned long thread64_t;

typedef struct {
    void (*start_routine)(void*);
    void *arg;
    int state;
    unsigned long parent_pid; // 親プロセスID
} thread_ctrl64_t;

#define MAX_THREAD64 8
static thread_ctrl64_t thread_table[MAX_THREAD64];

int thread_create64(thread64_t *tid, void (*start_routine)(void*), void *arg) {
    extern int proc_getpid64(void);
    for (int i = 0; i < MAX_THREAD64; i++) {
        if (thread_table[i].state == 0) {
            thread_table[i].start_routine = start_routine;
            thread_table[i].arg = arg;
            thread_table[i].state = 1;
            thread_table[i].parent_pid = proc_getpid64(); // 親プロセスIDを記録
            *tid = i;
            return 0;
        }
    }
    return -1;
}

int thread_join64(thread64_t tid) {
    // ダミー: 何もしない
    return 0;
}

int thread_exit64(void) {
    // ダミー: 何もしない
    return 0;
}
