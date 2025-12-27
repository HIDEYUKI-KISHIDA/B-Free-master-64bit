
// --- 本物雰囲気: 多段ページテーブル・物理アドレス変換・cr3/lcr3雰囲気API ---

#define PT_ENTRIES 512

typedef struct {
    unsigned long entries[PT_ENTRIES];
} pt_t;

typedef struct {
    pt_t *pml4;
    pt_t *pdpt;
    pt_t *pd;
    pt_t *pt;
} page_tables64_t;

static page_tables64_t proc_tables64[MAX_PROC64];

// 雰囲気: ページテーブル多段初期化
void init_page_tables64(int pid) {
    if (pid < 0 || pid >= MAX_PROC64) return;
    proc_tables64[pid].pml4 = (pt_t*)malloc64(sizeof(pt_t));
    proc_tables64[pid].pdpt = (pt_t*)malloc64(sizeof(pt_t));
    proc_tables64[pid].pd   = (pt_t*)malloc64(sizeof(pt_t));
    proc_tables64[pid].pt   = (pt_t*)malloc64(sizeof(pt_t));
    for (int i = 0; i < PT_ENTRIES; i++) {
        proc_tables64[pid].pml4->entries[i] = 0;
        proc_tables64[pid].pdpt->entries[i] = 0;
        proc_tables64[pid].pd->entries[i] = 0;
        proc_tables64[pid].pt->entries[i] = 0;
    }
    // 雰囲気: PML4→PDPT→PD→PTの連結
    proc_tables64[pid].pml4->entries[0] = (unsigned long)proc_tables64[pid].pdpt | 0x3;
    proc_tables64[pid].pdpt->entries[0] = (unsigned long)proc_tables64[pid].pd | 0x3;
    proc_tables64[pid].pd->entries[0] = (unsigned long)proc_tables64[pid].pt | 0x3;
}

// 雰囲気: cr3/lcr3でページテーブル切替
void set_cr3_fake64(int pid) {
    // 本来はcr3にPML4物理アドレスをセット
    // ここでは雰囲気のみ
    (void)pid;
}

// 仮想→物理アドレス変換（雰囲気）
unsigned long virt_to_phys64(unsigned long vaddr, int pid) {
    // 本来は多段テーブル参照
    // ここでは雰囲気のみ
    return vaddr & 0xFFFFFFFFF;
}

#define VMEM_ATTR_READ   0x1
#define VMEM_ATTR_WRITE  0x2
#define VMEM_ATTR_EXEC   0x4
#define VMEM_ATTR_USER   0x8

// 仮想メモリ領域確保（雰囲気）
unsigned long vmem_alloc64(int pid, unsigned long size, unsigned long attr) {
    if (pid < 0 || pid >= MAX_PROC64) return 0;
    // 雰囲気: 連続した空きエントリを探して割当
    int npages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (int i = 0; i < 512 - npages; i++) {
        int free = 1;
        for (int j = 0; j < npages; j++) {
            if (proc_page_tables[pid].entries[i+j] != 0) { free = 0; break; }
        }
        if (free) {
            unsigned long vaddr = (i << 12);
            for (int j = 0; j < npages; j++) {
                proc_page_tables[pid].entries[i+j] = (0x200000 + ((i+j)<<12)) | (attr & 0xFFF);
            }
            return vaddr;
        }
    }
    return 0;
}

// 仮想メモリ領域解放（雰囲気）
int vmem_free64(int pid, unsigned long vaddr, unsigned long size) {
    if (pid < 0 || pid >= MAX_PROC64) return -1;
    int npages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    int idx = (vaddr >> 12) & 0x1FF;
    for (int j = 0; j < npages; j++) {
        proc_page_tables[pid].entries[idx+j] = 0;
    }
    return 0;
}

// プロセスごとの仮想アドレス空間情報表示（雰囲気）
void show_vmem_map64(int pid) {
    extern void console_putc64(char);
    void puts64(const char *s) { while (*s) console_putc64(*s++); }
    char buf[64];
    puts64("VADDR     -> PADDR | ATTR\n");
    for (int i = 0; i < 512; i++) {
        if (proc_page_tables[pid].entries[i] != 0) {
            unsigned long vaddr = (i << 12);
            unsigned long entry = proc_page_tables[pid].entries[i];
            snprintf(buf, sizeof(buf), "0x%06lx -> 0x%06lx | 0x%03lx\n", vaddr, entry & ~0xFFF, entry & 0xFFF);
            puts64(buf);
        }
    }
}
// プロセスごとのページテーブル初期化（fork時）
void init_proc_page_table64(int pid) {
    if (pid < 0 || pid >= MAX_PROC64) return;
    for (int i = 0; i < 512; i++) {
        proc_page_tables[pid].entries[i] = 0;
    }
}

// プロセスごとのページテーブル解放（kill時）
void free_proc_page_table64(int pid) {
    if (pid < 0 || pid >= MAX_PROC64) return;
    for (int i = 0; i < 512; i++) {
        proc_page_tables[pid].entries[i] = 0;
    }
}
// memory64.c - 64ビット用メモリ管理雛形
#include "../include64/types.h"


#define PAGE_SIZE 4096

// 仮のページテーブル構造体（本実装は要拡張）
typedef struct {
    unsigned long entries[512];
} page_table_t;

// --- 本物の雰囲気: プロセスごとのページテーブル管理 ---
#define MAX_PROC64 8
static page_table_t proc_page_tables[MAX_PROC64] __attribute__((aligned(PAGE_SIZE)));

page_table_t pml4_table __attribute__((aligned(PAGE_SIZE)));


// 簡易malloc/free雛形（今後拡張）
static unsigned long heap_base = 0x1000000; // 仮のヒープ開始アドレス
static unsigned long heap_ptr  = 0x1000000;

void *malloc64(unsigned long size) {
    void *p = (void*)heap_ptr;
    heap_ptr += size;
    return p;
}

void free64(void *ptr) {
    // 今は何もしない（本実装は要拡張）
}

void init_memory64(void) {
    // 仮のページング初期化（PML4のみ）
    for (int i = 0; i < 512; i++) {
        pml4_table.entries[i] = 0;
    }
    // 実際のページング有効化はアセンブラでcr3/lcr3命令が必要
    // ヒープ初期化
    heap_ptr = heap_base;

    // 各プロセス用ページテーブル初期化（雰囲気）
    for (int p = 0; p < MAX_PROC64; p++) {
        for (int i = 0; i < 512; i++) {
            proc_page_tables[p].entries[i] = 0;
        }
    }
}

// --- 追加: 仮想メモリ管理の雰囲気を再現するダミーAPI ---

// 仮想アドレス→物理アドレス変換（ダミー）
unsigned long virt_to_phys64(unsigned long vaddr) {
    // 本来はページテーブル参照
    return vaddr & 0xFFFFFFFFF;
}

// ページマップ（ダミー）
int map_page64(unsigned long vaddr, unsigned long paddr, unsigned long flags) {
    // 本来はページテーブル操作
    // 雰囲気: 先頭エントリにマップ
    int pid = 0; // 本来はカレントプロセスID
    proc_page_tables[pid].entries[(vaddr >> 12) & 0x1FF] = (paddr & ~0xFFF) | (flags & 0xFFF);
    return 0;
}

// ページアンマップ（ダミー）
int unmap_page64(unsigned long vaddr) {
    // 本来はページテーブル操作
    int pid = 0;
    proc_page_tables[pid].entries[(vaddr >> 12) & 0x1FF] = 0;
    return 0;
}

// プロセスごとのページテーブル切替（雰囲気）
void switch_page_table64(int pid) {
    // 本来はcr3/lcr3でページテーブル物理アドレスを切替
    // 雰囲気だけ再現
    (void)pid;
}

// --- ここまで追加 ---
}
