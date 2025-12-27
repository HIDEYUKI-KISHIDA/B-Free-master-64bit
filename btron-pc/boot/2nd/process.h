#ifndef __PROCESS_H__
#define __PROCESS_H__

#include "types.h"

/* プロセス状態 */
#define PROC_FREE       0
#define PROC_READY      1
#define PROC_RUNNING    2
#define PROC_BLOCKED    3
#define PROC_SLEEPING   4
#define PROC_DEAD       5

/* プロセスの最大数 */
#define MAX_PROCESS     64

/* ファイルディスクリプタの最大数 */
#define NOFILE          32

/* プロセス優先度 */
#define PRIO_MIN        0
#define PRIO_MAX        39
#define PRIO_DEFAULT    20

/* タイムスライス（ティック） */
#define TIME_SLICE      10

/* MMUストラクチャ（メモリ管理） */
struct mm_struct {
    UL64PTR pgdir;              /* ページテーブルディレクトリ（PML4） */
    UL64PTR stack_base;         /* スタック開始アドレス */
    UL64PTR stack_limit;        /* スタック終了アドレス */
    UL64PTR heap_base;          /* ヒープ開始アドレス */
    UL64PTR heap_top;           /* ヒープ現在位置 */
    int heap_size;              /* ヒープサイズ */
};

/* レジスタコンテキスト（割り込み時に保存） */
struct regs {
    UWORD64 rax, rcx, rdx, rbx;
    UWORD64 rbp, rsi, rdi;
    UWORD64 r8, r9, r10, r11;
    UWORD64 r12, r13, r14, r15;
    
    /* 割り込みスタックフレーム */
    UWORD64 rip;
    UWORD64 cs;
    UWORD64 rflags;
    UWORD64 rsp;
    UWORD64 ss;
};

/* プロセスディスクリプタ */
struct process {
    int pid;                    /* プロセスID */
    int parent_pid;             /* 親プロセスID */
    int state;                  /* プロセス状態 */
    int priority;               /* 優先度 */
    int ticks;                  /* 残りタイムスライス */
    
    struct regs regs;           /* レジスタコンテキスト */
    struct mm_struct *mm;       /* メモリ管理 */
    
    UWORD64 kstack;             /* カーネルスタック */
    UWORD64 ustack;             /* ユーザースタック */
    
    /* ファイルディスクリプタテーブル */
    void *file_table[NOFILE];   /* ファイルディスクリプタ配列 */
    
    int exit_code;              /* 終了コード */
};

/* グローバルプロセステーブル */
extern struct process proc_table[MAX_PROCESS];
extern int current_pid;
extern struct process *current_proc;

/* プロセス管理関数 */
int process_init(void);
int process_create(const char *name, UL64PTR entry, int priority);
int process_destroy(int pid);
struct process *get_process(int pid);
int get_free_pid(void);
int get_free_proc_slot(void);

/* コンテキスト操作 */
void save_context(struct process *proc, struct regs *regs);
void restore_context(struct process *proc);
void init_process_stack(struct process *proc, UL64PTR entry);

/* スケジューラから呼ばれる */
void switch_to_process(struct process *new_proc);

#endif  /* __PROCESS_H__ */
