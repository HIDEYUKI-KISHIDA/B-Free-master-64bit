// シグナル送信（ダミー）
int proc_sigsend64(int pid, int sig) {
    // 何もしない
    return 0;
}

// シグナル発生（ダミー）
int proc_sigraise64(int sig) {
    // 何もしない
    return 0;
}

// シグナルハンドラ設定（ダミー）
int proc_sighandler64(int sig, void *handler) {
    // 何もしない
    return 0;
}
// 現在のプロセスID（ダミー: 0固定）
int proc_getpid64(void) {
    return current_pid;
}

// プロセス優先度設定（ダミー）
int proc_setpriority64(int prio) {
    // 何もしない
    return 0;
}

// プロセスsleep（ダミー）
int proc_sleep64(int t) {
    volatile int i;
    for (i = 0; i < t * 1000000; i++) { __asm__ __volatile__("nop"); }
    return 0;
}
// 現在の生存プロセス数
int get_proc_count64(void) {
    int cnt = 0;
    for (int i = 0; i < MAX_PROC64; i++) {
        if (proc_table[i].state == 1) cnt++;
    }
    return cnt;
}
// process64.c - 64ビット用 プロセス管理雛形
#include "../include64/types.h"

#define MAX_PROC64 8

typedef struct {
    u64 pid;
    u64 state;
    u64 kstack;
    u64 regs[16]; // 汎用レジスタ保存用
    u64 pc;       // プログラムカウンタ
    u64 sp;       // スタックポインタ
    u64 page_table; // 仮想メモリ用
    u64 cr3;      // ページテーブル物理アドレス
    u64 parent_pid; // 親プロセスID
    u64 tid;      // スレッドID（0=メイン）
    u64 stack_base; // スタックベース
    u64 stack_size; // スタックサイズ
    // ファイルディスクリプタ等も追加可
} proc64_t;

static proc64_t proc_table[MAX_PROC64];
static u64 current_pid = 0;

void process_init64(void) {
    for (int i = 0; i < MAX_PROC64; i++) {
        proc_table[i].pid = i;
        proc_table[i].state = 0; // 0:空き, 1:実行中, 2:停止
        proc_table[i].kstack = 0;
    }
    // PID0は常に生存
    proc_table[0].state = 1;
    current_pid = 0;
}

// 空きスロットにプロセス追加（fork用）
int fork_proc64(void) {
    extern void init_proc_page_table64(int pid);
    extern void init_page_tables64(int pid);
    for (int i = 1; i < MAX_PROC64; i++) {
        if (proc_table[i].state == 0) {
            proc_table[i].state = 1;
            proc_table[i].kstack = 0x1000 * i;
            proc_table[i].parent_pid = current_pid;
            proc_table[i].tid = 0;
            proc_table[i].stack_base = 0x800000 + 0x10000 * i;
            proc_table[i].stack_size = 0x10000;
            init_proc_page_table64(i); // ページテーブル初期化
            init_page_tables64(i);     // 多段ページテーブル初期化
            proc_table[i].cr3 = (u64)proc_table[i].page_table; // 雰囲気
            return proc_table[i].pid;
        }
    }
    return -1;
}

// 最後の生きているプロセスをkill（PID0以外）
int kill_proc64(void) {
    extern void free_proc_page_table64(int pid);
    for (int i = MAX_PROC64-1; i > 0; i--) {
        if (proc_table[i].state == 1) {
            proc_table[i].state = 2;
            free_proc_page_table64(i); // プロセス終了時にページテーブル解放
            return proc_table[i].pid;
        }
    }
    return -1;
}

void schedule64(void) {
    // ラウンドロビンで次の実行可能プロセスを選択
    u64 next = (current_pid + 1) % MAX_PROC64;
    for (int i = 0; i < MAX_PROC64; i++) {
        if (proc_table[next].state == 1) {
            context_switch64(next);
            return;
        }
        next = (next + 1) % MAX_PROC64;
    }
}

void context_switch64(u64 next_pid) {
    // 本物の雰囲気: 現在のレジスタ保存・次のレジスタ復元・ページテーブル切替
    static u64 dummy_regs[16] = {0};
    for (int i = 0; i < 16; i++) {
        proc_table[current_pid].regs[i] = dummy_regs[i];
    }
    proc_table[current_pid].pc = 0x1000 * current_pid;
    proc_table[current_pid].sp = proc_table[current_pid].kstack + 0x800;
    // ページテーブル切替（雰囲気）
    extern void set_cr3_fake64(int pid);
    set_cr3_fake64(next_pid);
    // 次のプロセスのレジスタを復元（雰囲気）
    for (int i = 0; i < 16; i++) {
        dummy_regs[i] = proc_table[next_pid].regs[i];
    }
    // PC/SPも復元（雰囲気）
    // ...
    current_pid = next_pid;
}

// プロセステーブルの内容を表示（シェル用）
void show_proc_table64(void) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    char buf[64];
    puts64("PID   STATE   KSTACK\n");
    for (int i = 0; i < MAX_PROC64; i++) {
        snprintf(buf, sizeof(buf), "%2llu   %2llu      0x%llx\n", proc_table[i].pid, proc_table[i].state, proc_table[i].kstack);
        puts64(buf);
    }
}

// --- 追加: exec64/ユーザーランド実行の雰囲気を再現するダミーAPI ---

// プロセスイメージ切替（ダミー: 状態だけ変化）
int exec_proc64(const char *image) {
    // 本来はバイナリロード・メモリマップ・レジスタ初期化等
    // ここでは雰囲気だけ再現
    // 現在のプロセスIDのイメージ名を記録（省略）
    // 状態を"実行中"に
    proc_table[current_pid].state = 1;
    return 0;
}

// プロセス空間分離の雰囲気（ダミー: 何もしない）
void switch_user_space64(u64 pid) {
    // 本来はページテーブル切替等
    // 雰囲気だけ再現
    (void)pid;
}

// --- ここまで追加 ---
}
