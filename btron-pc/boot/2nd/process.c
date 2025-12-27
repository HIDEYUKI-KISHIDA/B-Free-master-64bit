/* POSIX風 fork/exec/wait 雛形 */
int sys_fork(void) {
    // プロセス生成（メモリ・スタック・ファイルテーブル複製）
    if (!current_proc) return -1;
    int slot = get_free_proc_slot();
    if (slot == -1) return -1;
    int pid = get_free_pid();
    struct process *parent = current_proc;
    struct process *child = &proc_table[slot];
    memset(child, 0, sizeof(struct process));
    child->pid = pid;
    child->parent_pid = parent->pid;
    child->state = PROC_READY;
    child->priority = parent->priority;
    child->ticks = TIME_SLICE;
    // メモリ空間複製（簡易: 実際はページテーブル複製）
    if (parent->mm) {
        child->mm = (struct mm_struct *)malloc64(sizeof(struct mm_struct));
        memcpy(child->mm, parent->mm, sizeof(struct mm_struct));
        child->mm->pgdir = (UL64PTR)malloc64(4096);
        memcpy((void *)child->mm->pgdir, (void *)parent->mm->pgdir, 4096);
    }
    // カーネル/ユーザースタック複製
    child->kstack = (UWORD64)malloc64(4096);
    memcpy((void *)child->kstack, (void *)parent->kstack, 4096);
    child->ustack = (UWORD64)malloc64(4096);
    memcpy((void *)child->ustack, (void *)parent->ustack, 4096);
    // ファイルディスクリプタ複製
    for (int i = 0; i < NOFILE; i++) {
        child->file_table[i] = parent->file_table[i];
    }
    child->exit_code = 0;
    init_process_stack(child, parent->entry);
    return pid;
}

int sys_exec(int pid, void *entry) {
    // 実行開始
    if (pid < 0 || pid >= MAX_PROCESS) return -1;
    struct process *proc = &proc_table[pid];
    proc->state = PROC_RUNNING;
    proc->entry = entry;
    // x86_64: ページテーブル切替、スタック・RIPセット、ユーザー空間ジャンプ
    // (雛形: 実際はアセンブリでCR3, RSP, RIPをセット)
    // asm("mov %0, %%cr3; mov %1, %%rsp; jmp *%2" : : "r"(proc->mm->pgdir), "r"(proc->ustack), "r"(entry));
    // ここでは模擬的に関数呼び出し
    // user_entry(proc->ustack);
    return 0;
}

int sys_wait(int pid) {
    // 子プロセス終了待ち
    if (pid < 0 || pid >= MAX_PROCESS) return -1;
    struct process *proc = &proc_table[pid];
    while (proc->state != PROC_ZOMBIE) {
        // ポーリング（本来はイベント/割り込み）
    }
    return proc->exit_code;
}

// 標準Cライク関数はlib.cで本体実装し、ここではextern宣言のみに統一
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
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *d, const void *s, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern int printf(const char *fmt, ...);
extern int puts(const char *s);
extern int putchar(int c);
extern int getchar(void);
extern int sprintf(char *str, const char *fmt, ...);
extern int snprintf(char *str, size_t size, const char *fmt, ...);
extern int sscanf(const char *str, const char *fmt, ...);
extern void abort(void);
extern void exit(int code);
extern int atexit(void (*f)(void));
extern int setjmp(jmp_buf env);
extern void longjmp(jmp_buf env, int val);
#endif
#include "process.h"
#include "lib.h"
#include "memory64.h"
#include <stdint.h>
// #include "page64.h"

/* グローバルプロセステーブル */
struct process proc_table[MAX_PROCESS];
int current_pid = 0;
struct process *current_proc = NULL;

/* プロセステーブル初期化 */
int process_init(void)
{
    int i;
    
    /* 全プロセスを未使用に設定 */
    for (i = 0; i < MAX_PROCESS; i++) {
        proc_table[i].pid = -1;
        proc_table[i].state = PROC_FREE;
        proc_table[i].parent_pid = -1;
        proc_table[i].priority = PRIO_DEFAULT;
        proc_table[i].exit_code = 0;
    }
    
    return 0;
}

/* 空いているプロセスIDを取得 */
int get_free_pid(void)
{
    int i;
    int max_pid = 0;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].state != PROC_FREE) {
            if (proc_table[i].pid > max_pid) {
                max_pid = proc_table[i].pid;
            }
        }
    }
    
    return max_pid + 1;
}

/* 空いているプロセススロットを取得 */
int get_free_proc_slot(void)
{
    int i;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].state == PROC_FREE) {
            return i;
        }
    }
    
    return -1;  /* スロットなし */
}

/* PIDからプロセスを取得 */
struct process *get_process(int pid)
{
    int i;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_FREE) {
            return &proc_table[i];
        }
    }
    
    return NULL;
}

/* プロセス生成 */
int process_create(const char *name, UL64PTR entry, int priority)
{
    int slot;
    int pid;
    struct process *proc;
    struct mm_struct *mm;
    
    /* 空きスロット確保 */
    slot = get_free_proc_slot();
    if (slot == -1) {
        boot_printf("ERROR: Process table full\n");
        return -1;
    }
    
    /* PID割り当て */
    pid = get_free_pid();
    
    /* メモリ管理構造初期化 */
    mm = (struct mm_struct *)malloc64(sizeof(struct mm_struct));
    if (!mm) {
        boot_printf("ERROR: Failed to allocate mm_struct\n");
        return -1;
    }
    
    /* ページディレクトリ作成（ユーザー用） */
    mm->pgdir = (UL64PTR)malloc64(4096);
    if (!mm->pgdir) {
        boot_printf("ERROR: Failed to allocate page directory\n");
        free64((void *)mm);
        return -1;
    }
    
    /* スタック領域確保 */
    mm->stack_base = (UL64PTR)(uintptr_t)0x00007FFFFFFFE000;  /* ユーザー空間スタック */
    mm->stack_limit = (UL64PTR)(uintptr_t)0x00007FFFFFFF0000;
    /* ヒープ領域初期化 */
    mm->heap_base = (UL64PTR)(uintptr_t)0x0000000000500000;
    mm->heap_top = mm->heap_base;
    mm->heap_size = 0;
    
    /* プロセス構造体初期化 */
    proc = &proc_table[slot];
    proc->pid = pid;
    proc->parent_pid = current_pid;
    proc->state = PROC_READY;
    proc->priority = priority;
    proc->ticks = TIME_SLICE;
    proc->mm = mm;
    proc->exit_code = 0;
    
    /* カーネルスタック確保 */
    proc->kstack = (UWORD64)malloc64(4096);
    if (!proc->kstack) {
        boot_printf("ERROR: Failed to allocate kernel stack\n");
        free64((void *)mm->pgdir);
        free64((void *)mm);
        return -1;
    }
    
    /* ユーザースタック確保 */
    proc->ustack = (UWORD64)malloc64(4096);
    if (!proc->ustack) {
        boot_printf("ERROR: Failed to allocate user stack\n");
        free64((void *)proc->kstack);
        free64((void *)mm->pgdir);
        free64((void *)mm);
        return -1;
    }
    
    /* ファイルディスクリプタテーブル初期化 */
    int i;
    for (i = 0; i < NOFILE; i++) {
        proc->file_table[i] = NULL;
    }
    
    /* スタックとプログラムカウンタ初期化 */
    init_process_stack(proc, entry);
    
    boot_printf("Process created: PID=%d, Entry=0x%016lx, Priority=%d\n", 
                   pid, (unsigned long)entry, priority);
    
    return pid;
}

/* プロセス削除 */
int process_destroy(int pid)
{
    struct process *proc = get_process(pid);
    
    if (!proc) {
        return -1;
    }
    
    /* メモリ解放 */
    if (proc->mm) {
        if (proc->mm->pgdir) {
            free64((void *)proc->mm->pgdir);
        }
        free64((void *)proc->mm);
    }
    
    if (proc->kstack) {
        free64((void *)proc->kstack);
    }
    
    if (proc->ustack) {
        free64((void *)proc->ustack);
    }
    
    /* ファイルディスクリプタクローズ */
    int i;
    for (i = 0; i < NOFILE; i++) {
        if (proc->file_table[i]) {
            /* ファイルクローズ処理（後で実装） */
        }
    }
    
    /* プロセス状態を未使用に */
    proc->state = PROC_FREE;
    proc->pid = -1;
    
    boot_printf("Process destroyed: PID=%d\n", pid);
    
    return 0;
}

/* プロセススタック初期化 */
void init_process_stack(struct process *proc, UL64PTR entry)
{
    UWORD64 *sp;
    
    /* ユーザースタックポインタ初期化 */
    sp = (UWORD64 *)(proc->ustack + 4096 - 8);
    
    /* リターンアドレスをスタックに設定 */
    *sp = 0;  /* 実際にはプロセス終了時のアドレス */
    
    /* レジスタコンテキスト初期化 */
    proc->regs.rsp = (UWORD64)sp;
    proc->regs.rip = (UWORD64)(uintptr_t)entry;
    proc->regs.rflags = 0x202;  /* IF, IOPL=0, Reserved bits */
    
    /* その他のレジスタ初期化 */
    proc->regs.rax = 0;
    proc->regs.rbx = 0;
    proc->regs.rcx = 0;
    proc->regs.rdx = 0;
    proc->regs.rsi = 0;
    proc->regs.rdi = 0;
    proc->regs.rbp = (UWORD64)sp;
    
    /* セグメントレジスタはユーザーセグメント */
    proc->regs.cs = 0x23;   /* ユーザーコードセグメント */
    proc->regs.ss = 0x2B;   /* ユーザーデータセグメント */
}

/* コンテキスト保存 */
void save_context(struct process *proc, struct regs *regs)
{
    if (!proc || !regs) return;
    
    /* レジスタコンテキストをプロセス構造体にコピー */
    proc->regs = *regs;
}

/* コンテキスト復元 */
void restore_context(struct process *proc)
{
    if (!proc) return;
    
    current_proc = proc;
    current_pid = proc->pid;
    
    /* ページテーブルを設定（ユーザープロセスの場合） */
    if (proc->mm && proc->mm->pgdir) {
        /* CR3にページテーブルを設定 */
        /* asm volatile("mov %0, %%cr3" : : "r"(proc->mm->pgdir)); */
    }
}

/* プロセスに切り替え */
void switch_to_process(struct process *new_proc)
{
    if (!new_proc || new_proc->state == PROC_FREE) {
        return;
    }
    
    /* 古いプロセスのコンテキストを保存（既に割り込みハンドラで保存済み） */
    
    /* 新しいプロセスに切り替え */
    new_proc->state = PROC_RUNNING;
    new_proc->ticks = TIME_SLICE;
    
    restore_context(new_proc);
}

/* プロセスステータス表示（デバッグ用） */
void process_list(void)
{
    int i;
    const char *state_name[] = {
        "FREE", "READY", "RUNNING", "BLOCKED", "SLEEPING", "DEAD"
    };
    
    boot_printf("PID\tParent\tState\t\tPriority\tTicks\n");
    boot_printf("====================================================\n");
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].state != PROC_FREE) {
            boot_printf("%d\t%d\t%s\t%d\t%d\n",
                          proc_table[i].pid,
                          proc_table[i].parent_pid,
                          state_name[proc_table[i].state],
                          proc_table[i].priority,
                          proc_table[i].ticks);
        }
    }
}
