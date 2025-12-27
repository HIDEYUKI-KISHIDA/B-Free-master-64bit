// syscall64.c - 64ビット用 システムコール雛形
#include "../include64/types.h"

void syscall_init64(void) {
    // 本物の雰囲気: システムコールテーブル初期化
    // 本来はIDTにシステムコール割り込み(0x80等)を登録
    extern void printk64(const char*);
    printk64("[SYSCALL] syscall table/IDT setup\n");
}

// システムコール割り込み経由の雰囲気API
u64 syscall_dispatch64(int callno, void *arg) {
    // 本来は割り込みからカーネルに入り、callnoで分岐
    if (callno >= 0 && syscall_table64[callno].func) {
        return syscall_table64[callno].func(arg);
    }
    return (u64)-1;
}

u64 sys_write64(const char *buf, u64 len) {
    // TODO: 標準出力への書き出し
    return len;
}

u64 sys_exit64(u64 code) {
    // TODO: プロセス終了
    while (1) {}
    return 0;
}


// --- 追加: システムコールテーブル雛形と主要API（ダミー実装） ---

typedef u64 (*syscall_func64_t)(void);

u64 sys_open64(const char *path, u64 flags) { return 0; }
u64 sys_close64(u64 fd) { return 0; }
u64 sys_read64(u64 fd, char *buf, u64 len) { return len; }
u64 sys_ioctl64(u64 fd, u64 cmd, void *arg) { return 0; }
u64 sys_time64(u64 *t) { if (t) *t = 12345678; return 12345678; }
u64 sys_times64(void *buf) { return 0; }
u64 sys_sleep64(u64 sec) { return 0; }

typedef struct {
    const char *name;
    syscall_func64_t func;
} syscall_entry64_t;

static syscall_entry64_t syscall_table64[] = {
    {"write", (syscall_func64_t)sys_write64},
    {"exit", (syscall_func64_t)sys_exit64},
    {"open", (syscall_func64_t)sys_open64},
    {"close", (syscall_func64_t)sys_close64},
    {"read", (syscall_func64_t)sys_read64},
    {"ioctl", (syscall_func64_t)sys_ioctl64},
    {"time", (syscall_func64_t)sys_time64},
    {"times", (syscall_func64_t)sys_times64},
    {"sleep", (syscall_func64_t)sys_sleep64},
    {NULL, NULL}
};

// --- ここまで追加 ---
