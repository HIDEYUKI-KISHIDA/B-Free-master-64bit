// userland_arm/process_test_arm.c
// プロセス/スレッド管理動作テスト用サンプル
// 2025/12/27 新規作成

#include "../../btron-pc/include_arm/types_arm.h"
#include <stdio.h>

extern void process_init(void);
extern int process_create(void (*entry)(void));
extern int thread_create(int pid, void (*entry)(void));
extern void schedule(void);

void child_thread(void) {
    printf("子スレッド実行中\n");
}

void child_process(void) {
    printf("子プロセス実行中\n");
    int tid = thread_create(1, child_thread);
    if (tid >= 0) printf("子スレッド作成: tid=%d\n", tid);
}

int main(void) {
    printf("プロセス/スレッドテスト開始\n");
    process_init();
    int pid = process_create(child_process);
    if (pid >= 0) printf("子プロセス作成: pid=%d\n", pid);
    schedule();
    printf("プロセス/スレッドテスト終了\n");
    return 0;
}
