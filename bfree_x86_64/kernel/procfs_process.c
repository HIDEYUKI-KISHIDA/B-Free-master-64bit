#include <stdio.h>
#include <string.h>
#include "procfs.c"

// プロセス情報を/procに公開する雛形

typedef struct {
    int pid;
    const char *name;
    int state;
} proc_info_t;

#define PROCFS_PROCESS_MAX 64
static proc_info_t procfs_process_table[PROCFS_PROCESS_MAX];
static int procfs_process_count = 0;

// プロセス登録
int procfs_process_register(int pid, const char *name, int state) {
    if (procfs_process_count >= PROCFS_PROCESS_MAX) return -1;
    procfs_process_table[procfs_process_count].pid = pid;
    procfs_process_table[procfs_process_count].name = name;
    procfs_process_table[procfs_process_count].state = state;
    procfs_process_count++;
    // /proc/[pid] 形式で登録
    char path[32];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    procfs_register(strdup(path), name); // nameをvalueとして仮登録
    return 0;
}

// プロセス状態更新
int procfs_process_set_state(int pid, int state) {
    for (int i = 0; i < procfs_process_count; ++i) {
        if (procfs_process_table[i].pid == pid) {
            procfs_process_table[i].state = state;
            return 0;
        }
    }
    return -1;
}
