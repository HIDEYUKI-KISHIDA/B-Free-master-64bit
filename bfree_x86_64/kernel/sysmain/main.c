#include "vmm.h"
#include "elf_loader.h"
#include "../vbe_gop.h"

// user_hello_elf.c の内容は別ファイルで定義されているため、extern宣言のみ行う
extern unsigned char user_hello_elf[];
extern unsigned int user_hello_elf_size;

// --- Multiboot2情報アドレス受け渡し用グローバル変数 ---
#include <stdint.h>

// 必要な定数・マクロ定義
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096ULL
#endif
#ifndef MAX_PHYSICAL_ADDR
#define MAX_PHYSICAL_ADDR 0x100000000ULL // 4GiB
#endif
#ifndef BITMAP_SIZE
#define BITMAP_SIZE (MAX_PHYSICAL_ADDR / PAGE_SIZE / 8)
#endif

#ifndef BFREE_BOOT_DEBUG
#define BFREE_BOOT_DEBUG 0
#endif

// PMMビット操作マクロ
#define pmm_set_bit(idx)   (_pmm_bitmap_start[(idx) / 8] |=  (1 << ((idx) % 8)))
#define pmm_clear_bit(idx) (_pmm_bitmap_start[(idx) / 8] &= ~(1 << ((idx) % 8)))




// 必要な関数プロトタイプ宣言
void knl_main(void);
void uart_puts(const char *s);
void pmm_init_reservations(void);
void pmm_init_pt_pool(void);
void* pmm_alloc(void);
void pmm_free(void* addr);
char* kgets(char* buf, int size);

static void boot_halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static int boot_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static void boot_enter_emergency_shell(const char *reason)
{
    char cmd[64];
    uart_puts("[BOOT][FALLBACK] entering emergency shell: ");
    uart_puts(reason ? reason : "unknown");
    uart_puts("\n");
    uart_puts("[BOOT][FALLBACK] commands: retry halt\n");
    for (;;) {
        uart_puts("fallback> ");
        kgets(cmd, sizeof(cmd));
        if (boot_streq(cmd, "halt")) {
            uart_puts("[BOOT][FALLBACK] halt\n");
            boot_halt_forever();
        }
        if (boot_streq(cmd, "retry")) {
            uart_puts("[BOOT][FALLBACK] retry requested (not implemented)\n");
            continue;
        }
        uart_puts("[BOOT][FALLBACK] unknown command\n");
    }
}

// 64bit値を16桁16進数で出力
void uart_putc(char c); // プロトタイプ追加
void uart_puthex64(uint64_t val) {
    for (int s = 60; s >= 0; s -= 4) {
        char c = "0123456789ABCDEF"[(val >> s) & 0xF];
        uart_putc(c);
    }
}

// PMMビットマップ領域（リンカスクリプト等で確保されている前提）
extern uint8_t _pmm_bitmap_start[];
extern uint8_t _pmm_bitmap_end[];

uint32_t g_mb2_magic = 0;
uint32_t g_mb2_info = 0;

// アセンブラ側から呼ばれるエントリポイント
void bfree_entry(uint32_t mb2_magic, uint32_t mb2_info) {
    g_mb2_magic = mb2_magic;
    g_mb2_info = mb2_info;
    knl_main();
}

// --- Multiboot2/E820パースとPMM初期化雛形 ---
#include <stddef.h>
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t ext;
} __attribute__((packed)) e820_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    // e820_entry_t entries[]; // 可変長
} __attribute__((packed)) mb2_tag_mmap_t;

void pmm_init_from_multiboot2(uint8_t* mb2_addr) {
    uart_puts("[PMM] Multiboot2/E820 parse start\n");
    mb2_tag_t* tag = (mb2_tag_t*)(mb2_addr + 8); // 先頭8バイトは合計サイズ等
    while (tag->type != 0) {
        if (tag->type == 6) { // 6: memory map
            mb2_tag_mmap_t* mmap = (mb2_tag_mmap_t*)tag;
            size_t entries = (mmap->size - sizeof(mb2_tag_mmap_t)) / mmap->entry_size;
            uart_puts("[PMM] E820 entries: ");
            uart_puthex64(entries);
            uart_puts("\n");
            uint8_t* entry_ptr = (uint8_t*)mmap + sizeof(mb2_tag_mmap_t);
            for (size_t i = 0; i < entries; ++i) {
                e820_entry_t* e = (e820_entry_t*)entry_ptr;
                uart_puts("[PMM] E820: base=");
                uart_puthex64(e->base);
                uart_puts(" len=");
                uart_puthex64(e->length);
                uart_puts(" type=");
                uart_puthex64(e->type);
                uart_puts("\n");
                if (e->type == 1) { // Available
                    uint64_t start_pg = e->base / PAGE_SIZE;
                    uint64_t end_pg = (e->base + e->length) / PAGE_SIZE;
                    for (uint64_t p = start_pg; p < end_pg && p < (BITMAP_SIZE * 8); p++) {
                        pmm_clear_bit(p); // 空き
                    }
                    uart_puts("[PMM] Avail: ");
                    uart_puthex64(e->base);
                    uart_puts("-");
                    uart_puthex64(e->base + e->length);
                    uart_puts("\n");
                }
                entry_ptr += mmap->entry_size;
            }
        }
        // 8バイトアラインメントで次のタグへ
        tag = (mb2_tag_t*)(((uintptr_t)tag + tag->size + 7) & ~7ULL);
    }
    uart_puts("[PMM] Multiboot2/E820 parse end\n");
}
// Cエントリポイント（reset.Sから呼ばれる）
// void bfree_entry(void) { knl_main(); } // ←不要な重複定義を削除
// ...existing code...
#include <stdint.h>

// --- I/Oポート入力関数 ---
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// --- I/Oポート出力関数（8259A PIC等用）---
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// プロトタイプ宣言（pic_init, kputc, kgetsのimplicit declaration対策）
void pic_init(void);
void kputc(char c);
char* kgets(char* buf, int size);
// 共通カーネルヘッダ

#include "tk/kernel.h"
#include <stddef.h>
#include "../string.h"
#include "../device.h"
#ifndef device_t
// device_t型が見えるか確認
typedef int __check_device_t_typedef_missing[(sizeof(device_t) > 0) ? 1 : -1];
#endif

extern void ioapic_init(void);
void setup_task_stack(TCB *tcb, void (*entry)(void), uint64_t *stack, size_t stack_size);
/*
 * main.c - x86_64 T-Kernel2.0 カーネルメインエントリ
 * 仕様: TK2_x86_64_Spec.md v2.3準拠
 */

#include <stdint.h>

void knl_save_fpu_state(void *buf);
void knl_restore_fpu_state(void *buf);

void uart_init(uint32_t baud);
void uart_puts(const char *s);
void knl_cpu_init(void);
void knl_idt_init(void);
void knl_pic_init(void);
void knl_pic_enable(int irq);
void knl_timer_init(void);
void knl_start_dispatch(void);
void timer_handler(void);
uint64_t knl_get_current_time(void);

// タイマーIRQ (vector 32 = IRQ0) シム
static void timer_irq_shim(void *regs) {
    (void)regs;
    timer_handler();
    // EOIはknl_interrupt_mainが送信するため不要
}


// FPUテスト用タスク1
void task1(void) {
    uart_puts("[TASK1] start\n");
    volatile int a = 1, b = 2;
    volatile int c = 0;
    for (int i = 0; i < 5; ++i) {
        c = a + b + i;
        uart_puts("[TASK1] INT c=\n");
        for (volatile int d = 0; d < 1000000; ++d) {}
    }
    uart_puthex64((uint64_t)c);
    uart_puts("\n");
    uart_puts("[TASK1] end\n");
    for (;;) { __asm__ volatile ("hlt"); }
}
// ...existing code...

// FPUテスト用タスク2
void task2(void) {
    uart_puts("[TASK2] start\n");
    volatile int x = 10, y = 20;
    volatile int z = 0;
    for (int i = 0; i < 5; ++i) {
        z = x * y + i;
        uart_puts("[TASK2] INT z=\n");
        for (volatile int d = 0; d < 1000000; ++d) {}
    }
    uart_puthex64((uint64_t)z);
    uart_puts("\n");
    uart_puts("[TASK2] end\n");
    for (;;) { __asm__ volatile ("hlt"); }
}


#include "../include/tk/fpu_state.h"





TCB tcb1, tcb2;
TCB *knl_current_task = &tcb1;
typedef void (*task_func_t)(void);
task_func_t task_entry[2] = { task1, task2 };
int current_task_idx = 0;



void setup_task_stack(TCB *tcb, void (*entry)(void), uint64_t *stack, size_t stack_size) {
    #define USER_STACK_TOP 0x01400000
    /* PID1 init only — keep small (2048 pages here hung boot at "Building user stack"). */
    #define USER_STACK_PAGES 64
    // cpu_init.c の新GDT: [3]=null(0x18), [4]=UserData(0x20), [5]=UserCode(0x28), [6+7]=TSS(0x30)
    // sysretq: CS=STAR[63:48]+16=0x2B(GDT[5]), SS=STAR[63:48]+8=0x23(GDT[4])
    #define USER_CS 0x2B   // (5<<3)|3: ring3 code selector
    #define USER_SS 0x23   // (4<<3)|3: ring3 data selector
    uint8_t *top_stack_page = 0;
    uint64_t user_stack_top = USER_STACK_TOP;
    uint64_t user_rsp = USER_STACK_TOP;
#if defined(BFREE_ENABLE_ASLR) && BFREE_ENABLE_ASLR
    {
        uint64_t jitter_pages = (knl_get_current_time() & 0x7ULL); /* 0..7 pages */
        user_stack_top -= jitter_pages * PAGE_SIZE;
    }
#endif
    tcb->user_stack_top = user_stack_top;
    for (int i = 1; i <= USER_STACK_PAGES; ++i) {
        void *stack_phys = pmm_alloc();
        if (stack_phys && tcb->page_table_base) {
            uint64_t stack_vaddr = user_stack_top - i * PAGE_SIZE;
#if BFREE_BOOT_DEBUG
            uart_puts("[STACK] phys=");
            uart_puthex64((uint64_t)stack_phys);
            uart_puts(" vaddr=");
            uart_puthex64(stack_vaddr);
            uart_puts("\n");
#endif
            vmm_map_page((page_table_t*)tcb->page_table_base,
                stack_vaddr,
                (uint64_t)stack_phys,
                0x007ULL); // Present|RW|User
            bfree_kernel_zero_phys_page((uint64_t)(uintptr_t)stack_phys);
            if (i == 1) {
                top_stack_page = (uint8_t *)stack_phys;
            }
        }
    }
    if (top_stack_page) {
        uint64_t *user_sp = (uint64_t *)(top_stack_page + PAGE_SIZE);

        *--user_sp = 0; // AT_NULL value
        *--user_sp = 0; // AT_NULL type
        *--user_sp = 0; // envp terminator
        *--user_sp = 0; // argv terminator
        *--user_sp = 0; // argc

        user_rsp = user_stack_top - (uint64_t)((top_stack_page + PAGE_SIZE) - (uint8_t *)user_sp);
        user_rsp &= ~0xFULL;
    }
    uart_puts("[STACK] User stack pages mapped=");
    uart_puthex64(USER_STACK_PAGES);
    uart_puts(" top=");
    uart_puthex64(user_stack_top);
    uart_puts(" rsp=");
    uart_puthex64(user_rsp);
    uart_puts("\n");
    uint64_t *sp = &stack[stack_size];
    // iretqフレーム（下から積む: SS, RSP, RFLAGS, CS, RIP）
    *--sp = USER_SS;             // SS (ring3 user data)
    *--sp = user_rsp;            // RSP (ELF起動用に初期化したユーザースタック)
    *--sp = 0x202;               // RFLAGS
    *--sp = USER_CS;             // CS (ring3 user code)
    *--sp = (uint64_t)entry;     // RIP (エントリポイント)
    // knl_start_dispatch の pop 順: rbp, r15, r14, r13, r12, r11, r10, r9,
    // r8, rdi, rsi, rdx, rcx, rbx, rax
    for (int i = 0; i < 15; ++i) *--sp = 0x0;
    tcb->tskctxb.ssp = sp;
    for (size_t i = 0; i < sizeof(tcb->fpu.data); ++i) tcb->fpu.data[i] = 0;
}

static void init_task_page_table(TCB *tcb) {
    static page_table_t task_page_tables[2] __attribute__((aligned(4096), section(".bfree_page_table")));
    static int next_slot = 0;

    if (tcb->page_table_base) {
        return;
    }

    page_table_t *new_pt = &task_page_tables[next_slot++ % 2];
    vmm_clone_kernel_page_table(new_pt);
    tcb->page_table_base = new_pt;
}


// --- ELFローダによるユーザー空間バイナリのロード（デバッグ出力付き）は knl_main 内で実行 ---


// --- VGAテキストバッファ出力 ---
#define VGA_TEXT_BUF ((volatile uint16_t*)0xB8000)
void vga_clear(void) {
    for (int i = 0; i < 80 * 25; ++i) {
        VGA_TEXT_BUF[i] = 0x0720; // 黒地に白文字で空白
    }
}
void vga_puts(const char* s, int row) {
    int col = 0;
    while (*s && col < 80) {
        VGA_TEXT_BUF[row * 80 + col] = 0x0700 | *s++;
        col++;
    }
}

// --- カーネルCエントリポイント ---
// timer_manager.h, debug_trace.cを利用
#include "timer_manager.h"
#include "subsystem.h"
#include "vmm.h"
#include "security_policy.h"
void syslog(int priority, const char *format, ...);

#ifndef ENABLE_RUNTIME_NET
#define ENABLE_RUNTIME_NET 0
#endif

#ifndef BFREE_BOOT_GUI_FIRST
#define BFREE_BOOT_GUI_FIRST 0
#endif

static void __attribute__((unused)) test_timer_event(void *arg) {
    (void)arg;
    syslog(0, "[TIMER] 10秒経過イベント発火!");
}

static void timer_test_callback1(void *arg)
{
    (void)arg;
    syslog(0, "[TIMER] 3秒イベント!");
}

static void timer_test_callback2(void *arg)
{
    (void)arg;
    syslog(0, "[TIMER] 5秒イベント!");
}

static void timer_test_callback3(void *arg)
{
    (void)arg;
    syslog(0, "[TIMER] 10秒イベント!");
}

void knl_main(void) {
    // --- ユーザーland用タスクスタック確保 ---
    static uint64_t user_stack[4096]; // 16KBスタック
    void *user_entry = 0;
    extern void register_irq_handler(int irq, void* handler);

    // --- MB2 フレームバッファ情報をパース (低メモリのうちに実行) ---
    // Boot PT は VRAM identity map 済み → すぐ白地を出して黒画面を潰す
    extern void vbe_init_from_mb2(const uint8_t *);
    extern void fb_draw_splash(void);
    extern void fb_draw_splash_frame(uint32_t frame);
    extern void fb_run_boot_splash_anim(uint32_t cycles);
    vbe_init_from_mb2((const uint8_t *)(uintptr_t)g_mb2_info);
    fb_draw_splash(); /* earliest white + logo + spinner frame0 */

    // --- PMM を先に初期化しないと、user ELF / user stack が 0x0,0x1000...
    // を踏んで低物理メモリを壊す ---
    pmm_init_from_multiboot2((uint8_t*)(uintptr_t)g_mb2_info);
    pmm_init_reservations();
    pmm_init_pt_pool();
    uart_puts("[BOOT] Memory management initialization is complete.\n");
    bfree_security_init();
    if (bfree_verify_boot_stage("kernel.elf") != 0) {
        uart_puts("[PANIC][SECURITY] kernel stage verification failed\n");
        boot_enter_emergency_shell("verify-kernel-failed");
    }

    vmm_init_kernel_page_table();
    vmm_activate_kernel_page_table();
    /* Kernel PT drops boot MMIO maps — remap VRAM and re-paint before long init. */
    {
        extern page_table_t kernel_page_table;
        struct vbe_info vi;
        vbe_get_info(&vi);
        if (vi.vram_phys != 0 && vi.width != 0 && vi.height != 0 && vi.pitch != 0) {
            uintptr_t base = vi.vram_phys & ~(PAGE_SIZE - 1);
            uintptr_t end = (vi.vram_phys + (uintptr_t)vi.pitch * (uintptr_t)vi.height
                             + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            for (uintptr_t a = base; a < end; a += 0x200000ULL) {
                if (vmm_map_mmio_huge(&kernel_page_table, a, a) != 0)
                    break;
            }
            fb_draw_splash();
            uart_puts("[BOOT] Early brand splash (post-VMM)\n");
        }
    }
    init_task_page_table(&tcb1);
    extern void bfree_sysret_exec_globals_init(void);
    bfree_sysret_exec_globals_init();
    knl_cpu_init();
    knl_pic_init();
    knl_idt_init();
    register_irq_handler(14, vmm_page_fault_handler); // #PF

    // PIT タイマー直接設定 (IRQ0 = vector 32, ~1000Hz)
    outb(0x43, 0x36);         // PIT ch0, mode3, LSB/MSB
    outb(0x40, 1193 & 0xFF);  // count LSB
    outb(0x40, 1193 >> 8);    // count MSB
    knl_pic_enable(0);        // IRQ0 アンマスク
    register_irq_handler(32, timer_irq_shim);
    /* IRQ1 → vector 33 (PIC base 0x20). Required for PS/2 keyboard; dead code path below used to be the only registration. */
    extern void keyboard_irq_handler_shim(void *regs);
    register_irq_handler(33, keyboard_irq_handler_shim);
    __asm__ volatile ("sti");

    uart_puts("[BOOT] CPU and interrupt initialization is complete.\n");

    // --- サブシステム初期化 ---
    subsystem_init_all();
    uart_puts("[BOOT] Core subsystems are ready.\n");

    #if ENABLE_RUNTIME_NET
    extern void net_runtime_init(void);
    net_runtime_init();
    #endif

    // --- Phase 1 テスト: 64bit Serial I/O ---
    extern void test_x86_64_serial_dual_task(void);
    uart_puts("[TEST] Starting Phase 1 test...\n");
    test_x86_64_serial_dual_task();
    uart_puts("[TEST] Phase 1 test completed.\n");

    const char *boot_primary_elf = (BFREE_BOOT_GUI_FIRST ? "compositor.elf" : "init.elf");

    // --- Multiboot2 modules (initrd) をinitrd_registerに登録 ---
    // Multiboot2 type=3 タグ: mod_start, mod_end, string(コマンドライン/名前)
    {
        extern void initrd_register(const char *name, uint8_t *data, uint64_t size);
        typedef struct {
            uint32_t type; uint32_t size;
            uint32_t mod_start; uint32_t mod_end;
            char     string[0]; // NUL終端ファイル名
        } __attribute__((packed)) mb2_tag_module_t;
        uint8_t *mb2_base = (uint8_t*)(uintptr_t)g_mb2_info;
        if (mb2_base) {
            typedef struct { uint32_t type; uint32_t size; } mb2_tag_mini_t;
            mb2_tag_mini_t *tag = (mb2_tag_mini_t*)(mb2_base + 8);
            while (tag->type != 0) {
                if (tag->type == 3) { // MODULE
                    mb2_tag_module_t *m = (mb2_tag_module_t*)tag;
                    const char *name = (m->size > 16) ? m->string : "initrd.img";
                    // 名前からベースネーム取得（最後の'/'以降）
                    const char *basename = name;
                    for (const char *p = name; *p; ++p)
                        if (*p == '/') basename = p + 1;
                    uint64_t mod_size = m->mod_end - m->mod_start;
                    uart_puts("[INITRD] module: ");
                    uart_puts(basename);
                    uart_puts(" size=");
                    uart_puthex64(mod_size);
                    uart_puts("\n");
                    initrd_register(basename, (uint8_t*)(uintptr_t)m->mod_start, mod_size);
                    if (boot_streq(basename, "desktop_b4_probe.elf")) {
                        initrd_register("desktop.elf", (uint8_t*)(uintptr_t)m->mod_start, mod_size);
                        uart_puts("[INITRD] alias desktop.elf -> desktop_b4_probe.elf (B4)\n");
                    }
                    /* shell-only ISO: no init.elf module — alias shell.elf as init.elf for PID1.
                     * Never alias init.elf onto desktop.elf (breaks PID1; page fault at entry 0). */
                    if (boot_streq(basename, "shell.elf") && boot_streq(boot_primary_elf, "init.elf")) {
                        initrd_register("init.elf", (uint8_t*)(uintptr_t)m->mod_start, mod_size);
                        uart_puts("[INITRD] alias init.elf -> shell.elf (shell-only ISO)\n");
                    }
                }
                tag = (mb2_tag_mini_t*)(((uintptr_t)tag + tag->size + 7) & ~7ULL);
            }
        }
        uart_puts("[INITRD] module scan done\n");
        initrd_sync_user_page_tables();
    }

    // --- ユーザー空間ELFロード＆エントリポイント取得（初期化直後に実行） ---
    // 既定: init.elf（PID1 + FB UI）。無ければ shell.elf → user_hello.elf。
    uart_puts("[BOOT] Preparing to launch userland.\n");
    uart_puts("[KERNEL] Loading user ELF...\n");
    fb_draw_splash_frame(1);
    const char *target_elf = boot_primary_elf;
    int res = load_elf_image(target_elf, &user_entry, tcb1.page_table_base);
    if (res != 0) {
        uart_puts("[KERNEL] primary userland not found (code=");
        uart_puthex64((uint64_t)(int64_t)res);
        uart_puts("), fallback to shell.elf\n");
        target_elf = "shell.elf";
        fb_draw_splash_frame(2);
        res = load_elf_image(target_elf, &user_entry, tcb1.page_table_base);
        if (res != 0) {
            uart_puts("[KERNEL] shell.elf fallback failed, trying user_hello.elf\n");
            target_elf = "user_hello.elf";
            fb_draw_splash_frame(3);
            res = load_elf_image(target_elf, &user_entry, tcb1.page_table_base);
        }
    }
    fb_draw_splash_frame(4);
    /* load_elf_image restores the caller's CR3; boot needs the task PT active. */
    if (tcb1.page_table_base) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(tcb1.page_table_base) : "memory");
    }
    if (res == 0) {
        if (bfree_verify_boot_stage(target_elf) != 0) {
            uart_puts("[PANIC][SECURITY] user stage verification failed\n");
            boot_enter_emergency_shell("verify-user-failed");
        }
        if (boot_streq(target_elf, "init.elf")) {
            bfree_security_set_role(BFREE_ROLE_INIT);
        } else if (boot_streq(target_elf, "compositor.elf")) {
            bfree_security_set_role(BFREE_ROLE_COMPOSITOR);
        } else {
            bfree_security_set_role(BFREE_ROLE_APP);
        }
        uart_puts("[KERNEL] ELF loaded (");
        uart_puts(target_elf);
        uart_puts("). Entry: ");
        uart_puthex64((uint64_t)user_entry);
        uart_puts("\n");
        uart_puts("[USERLAND] Building the execution environment.\n");
        uart_puts("[USERLAND] Building user stack\n");
        setup_task_stack(&tcb1, user_entry, user_stack, 4096);
        uart_puts("[USERLAND] User stack ready\n");
        knl_current_task = &tcb1;
        current_task_idx = 0;
        page_table_t *task_pt = (page_table_t *)tcb1.page_table_base;
        uart_puts("[USERLAND] Verifying task page tables\n");
        uart_puts("[VMM][TASK] PML4E=");
        uart_puthex64(task_pt->pml4[0]);
        uart_puts(" PDPTE=");
        uart_puthex64(task_pt->pdpt[0]);
        uart_puts(" PDE2=");
        uart_puthex64(task_pt->pd[2]);
        uart_puts(" PTE[2][0]=");
        uart_puthex64(task_pt->pt[2][0]);
        uart_puts("\n");
        uart_puts("[USERLAND] Task page tables ready\n");
        uart_puts("[USERLAND] Execution environment is ready.\n");
        uart_puts("[USERLAND] Preparing CR3 switch\n");
        uart_puts("[VMM][START] CR3=");
        uart_puthex64((uint64_t)tcb1.page_table_base);
        uart_puts("\n");
        __asm__ volatile ("cli" : : : "memory");
        uart_puts("[USERLAND] Interrupts disabled for handoff\n");

        // --- フレームバッファ スプラッシュ描画 ---
        // vmm_init_kernel_page_table() で boot PT が破棄されているため
        // VRAM 物理ページを kernel_page_table に明示的にマップしてから描画する
        {
            extern void vbe_get_info(struct vbe_info *info);
            extern void fb_draw_splash(void);
            extern page_table_t kernel_page_table;
            struct vbe_info vi;
            vbe_get_info(&vi);
            if (vi.vram_phys != 0 && vi.width != 0 && vi.height != 0) {
                // pitch * height バイト分だけ 4KB ページを identity map
                uintptr_t base = vi.vram_phys & ~(PAGE_SIZE - 1);
                uintptr_t end  = (vi.vram_phys + vi.pitch * vi.height
                                  + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                for (uintptr_t a = base; a < end; a += 0x200000ULL) {
                    if (vmm_map_mmio_huge(&kernel_page_table, a, a) != 0) {
                        uart_puts("[SPLASH] MMIO map failed va=");
                        uart_puthex64((uint64_t)a);
                        uart_puts("\n");
                        break;
                    }
                }
                uart_puts("[SPLASH] vram_phys=0x");
                uart_puthex64((uint64_t)vi.vram_phys);
                uart_puts(" pitch=");
                uart_puthex64((uint64_t)vi.pitch);
                uart_puts(" w=");
                uart_puthex64((uint64_t)vi.width);
                uart_puts(" h=");
                uart_puthex64((uint64_t)vi.height);
                uart_puts("\n");
                uart_puts("[SPLASH] VRAM mapped, drawing brand splash...\n");
                /* Already painted early; one smooth cycle before ring3. */
                fb_run_boot_splash_anim(1);
                uart_puts("[BOOT] Framebuffer splash drawn.\n");
                uart_puts("[BOOT] Brand splash anim done -> ring3 init.\n");
            } else {
                uart_puts("[SPLASH] vbe_info invalid, skip.\n");
            }
        }

        /* Blue mock desktop / mid FB phases removed — brand splash only. */
        // CR3 切替してユーザー空間へ
        uart_puts("[USERLAND] Switching CR3 to task page table: ");
        uart_puthex64((uint64_t)tcb1.page_table_base);
        uart_puts("\n");
        __asm__ volatile ("mov %0, %%cr3" :: "r"(tcb1.page_table_base) : "memory");
        uart_puts("[USERLAND] Address space switched\n");
        uart_puts("[KERNEL] Jumping to userland...\n");
        knl_start_dispatch();

        // ここに戻るのは異常系
        uart_puts("[PANIC][STAGE=userland-dispatch] knl_start_dispatch returned unexpectedly.\n");
        boot_enter_emergency_shell("dispatch-returned");
    } else {
        uart_puts("[PANIC][STAGE=userland-load] User ELF load failed (shell.elf + user_hello.elf)\n");
        boot_enter_emergency_shell("user-elf-load-failed");
    }
            // --- センサデバイス登録（例：温度センサ） ---
            extern void register_sensor_device(int type);
            register_sensor_device(0); // type=0:温度センサ
        // --- RTCデバイス登録 ---
        extern void register_rtc_device(int type);
        register_rtc_device(0); // type=0:CMOS
    uart_puts("A: cli\n");
    __asm__ volatile ("cli");

    // --- VMM: ページテーブル初期化・デバッグ出力 ---

    vmm_init_kernel_page_table();
    vmm_dump_kernel_page_table();

    // --- VMM APIテスト: 仮想アドレス0x2000に物理0x100000をRWでマッピング ---
    extern page_table_t kernel_page_table;
    vmm_map_page(&kernel_page_table, 0x2000, 0x100000, 0x003ULL); // Present|RW
    // --- アンマップテスト ---
    vmm_unmap_page(&kernel_page_table, 0x2000);
    // --- 再マッピングテスト ---
    vmm_map_page(&kernel_page_table, 0x2000, 0x100000, 0x003ULL);

    // --- PMM動的確保・解放テスト ---
    uart_puts("[PMM-TEST] alloc/free test start\n");
    void* test_pages[4];
    for (int i = 0; i < 4; ++i) {
        test_pages[i] = pmm_alloc();
        uart_puts("[PMM-TEST] alloc page: ");
        uart_puthex64((uint64_t)test_pages[i]);
        uart_puts("\n");
    }
    for (int i = 0; i < 4; ++i) {
        uart_puts("[PMM-TEST] free page: ");
        uart_puthex64((uint64_t)test_pages[i]);
        uart_puts("\n");
        pmm_free(test_pages[i]);
    }
    uart_puts("[PMM-TEST] alloc/free test end\n");
    uart_puts("B: PIC\n");
    knl_pic_init();
    uart_puts("C: IDT\n");
    knl_idt_init();
    uart_puts("D: TIMER\n");
    knl_timer_init(); // タイマ割り込み初期化を追加
    uart_puts("E: IDT Initialized. Debug base ready.\n");
    // --- シリアルデバイス登録 ---
    extern void register_serial_device(void);
    register_serial_device();
    // --- PCIデバイス列挙 ---
    extern void pci_scan_and_register_devices(void);
    pci_scan_and_register_devices();
    uart_puts("F: Before STI\n");
    outb(0x21, 0xFC); // IRQ0, IRQ1を許可
    while (inb(0x64) & 0x01) { inb(0x60); }
    // 必要ならキーボード有効化: outb(0x60, 0xF4);
    uart_puts("G: STI\n");
    __asm__ volatile ("sti");
    uart_puts("H: Interrupts Enabled\n");
    extern void register_irq_handler(int irq, void* handler);
    extern void keyboard_irq_handler_shim(void *regs);
    register_irq_handler(33, keyboard_irq_handler_shim);
    vga_clear();
    vga_puts("VGA: Hello, B-Free x86_64!", 1);
    extern uint8_t cursor_x, cursor_y;
    cursor_y = 2;
    cursor_x = 0;
    uart_init(115200);

    // --- フレームバッファ スプラッシュ描画 (uart_init & PCI scan 後) ---
    // PCI scan 後に GPU BAR が確定するので、ここで描画する
    {
        extern void fb_run_boot_splash_anim(uint32_t cycles);
        fb_run_boot_splash_anim(1);
    }
    uart_puts("[BOOT] Framebuffer splash drawn.\n");

    uart_puts("OK\n");
    // ここで全デバイス一覧を出力
    print_all_devices();

        // --- 10秒後にメッセージを出すテストイベント登録 ---

    extern int timer_set_event(uint64_t expire_time, void (*callback)(void *), void *arg);
    extern uint64_t knl_get_current_time(void);
    uint64_t now = knl_get_current_time();
    timer_set_event(now +  3000000ULL, timer_test_callback1, 0); // 3秒後
    timer_set_event(now +  5000000ULL, timer_test_callback2, 0); // 5秒後
    timer_set_event(now + 10000000ULL, timer_test_callback3, 0); // 10秒後

    // --- 簡易CLIループ（kgetsで入力受付＆エコーバック） ---
    char buf[128];
    while (1) {
        kputc('>');
        kputc(' ');
        kgets(buf, sizeof(buf));
        // シリアルデバイスに送信テスト
        extern device_t *find_device(const char *name);
        device_t *dev = find_device("serial0");
        if (dev && dev->ops && dev->ops->open) dev->ops->open(dev, 0);
        if (dev && dev->ops && dev->ops->write) dev->ops->write(dev, buf, strlen(buf));
        if (dev && dev->ops && dev->ops->write) dev->ops->write(dev, "\r\n", 2);
    }
}

// --- PMMビットマップ初期化ルーチン ---
extern uint8_t _pmm_bitmap_start[];
#define APIC_BASE  0xFEE00000ULL
#define IOAPIC_BASE 0xFEC00000ULL
#define APIC_LEN   0x100000ULL // 1MB確保（十分な余裕）
#define KERNEL_RESERVED_BASE 0x00000000ULL
/* Legacy floor; reservations below also cover .bss past 4MiB (page_table_t ~0.65MiB
 * each) through _pmm_bitmap_end so pmm_free rewind cannot hand BSS frames to
 * guests (wipes PT / page_buf → zero text / #UD on pipe AS-copy). */
#define KERNEL_RESERVED_LEN  0x00400000ULL
#define KERNEL_STACK_BASE 0x110000ULL
#define KERNEL_STACK_LEN  0x10000ULL

extern uint8_t _bss_start[];
extern uint8_t _bss_end[];
extern uint8_t _stack_start[];
extern uint8_t _stack_end[];

static uint64_t g_pmm_alloc_start_page = 0;

void pmm_reserve_range(uint64_t base, uint64_t len, const char* label) {
    uint64_t start_pg = base / PAGE_SIZE;
    uint64_t end_pg = (base + len + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = start_pg; p < end_pg && p < (BITMAP_SIZE * 8); p++) {
        pmm_set_bit(p);
    }
    uart_puts("[PMM] Reserved ");
    uart_puts(label);
    uart_puts(" pages=");
    uart_puthex64(end_pg - start_pg);
    uart_puts(" base=");
    uart_puthex64(base);
    uart_puts(" len=");
    uart_puthex64(len);
    uart_puts("\n");
}


// --- PMM動的メモリ確保・解放API ---
// ページ単位で空きを探して確保・解放する簡易実装
/* pt_ext pages must live below 320 MiB for identity-mapped PTE stores. */
#define PMM_IDENTITY_PAGE_LIMIT ((uint64_t)160 * (0x200000ULL / 4096ULL))
#define PMM_PT_POOL_PAGES       256U

static uint64_t g_pmm_pt_pool[PMM_PT_POOL_PAGES];
static unsigned g_pmm_pt_pool_total;
static unsigned g_pmm_pt_pool_used;

void pmm_init_pt_pool(void)
{
    uint64_t i;
    unsigned found = 0;

    g_pmm_pt_pool_total = 0;
    g_pmm_pt_pool_used = 0;
    for (i = g_pmm_alloc_start_page;
         i < PMM_IDENTITY_PAGE_LIMIT && found < PMM_PT_POOL_PAGES;
         ++i) {
        if (!(_pmm_bitmap_start[i / 8] & (1 << (i % 8)))) {
            pmm_set_bit(i);
            g_pmm_pt_pool[found++] = i;
        }
    }
    g_pmm_pt_pool_total = found;
    uart_puts("[PMM] PT-POOL pages=");
    uart_puthex64(found);
    if (found > 0) {
        uart_puts(" lo=");
        uart_puthex64(g_pmm_pt_pool[0] * PAGE_SIZE);
        uart_puts(" hi=");
        uart_puthex64((g_pmm_pt_pool[found - 1] + 1ULL) * PAGE_SIZE);
    }
    uart_puts("\n");
}

void *pmm_alloc_pt_page(void)
{
    uint64_t i;

    if (g_pmm_pt_pool_used < g_pmm_pt_pool_total) {
        uint64_t pg = g_pmm_pt_pool[g_pmm_pt_pool_used++];
        return (void *)(pg * PAGE_SIZE);
    }
    for (i = g_pmm_alloc_start_page; i < PMM_IDENTITY_PAGE_LIMIT; ++i) {
        if (!(_pmm_bitmap_start[i / 8] & (1 << (i % 8)))) {
            pmm_set_bit(i);
            return (void *)(i * PAGE_SIZE);
        }
    }
    uart_puts("[PMM] PT page alloc failed (identity band full)\n");
    return 0;
}

void* pmm_alloc(void) {
    extern uint64_t g_bfree_shell_text_phys;
    extern int bfree_shell_page_pinned(uint64_t phys);
    for (uint64_t i = g_pmm_alloc_start_page; i < BITMAP_SIZE * 8; ++i) {
        if (!(_pmm_bitmap_start[i / 8] & (1 << (i % 8)))) {
            uint64_t page = i * PAGE_SIZE;
            if ((g_bfree_shell_text_phys != 0 && page == g_bfree_shell_text_phys) ||
                bfree_shell_page_pinned(page)) {
                uart_puts("[PMM] FATAL: alloc reuses shell text phys=");
                uart_puthex64(page);
                uart_puts("\n");
                pmm_set_bit(i);
                g_pmm_alloc_start_page = i + 1;
                continue;
            }
#if BFREE_BOOT_DEBUG
            uart_puts("[PMM][ALLOC] idx=");
            uart_puthex64(i);
            uart_puts(" addr=");
            uart_puthex64(page);
            uart_puts(" before=");
            uart_puthex64(_pmm_bitmap_start[i / 8]);
            uart_puts("\n");
#endif
            pmm_set_bit(i);
            g_pmm_alloc_start_page = i + 1;
#if BFREE_BOOT_DEBUG
            uart_puts("[PMM][ALLOC] idx=");
            uart_puthex64(i);
            uart_puts(" after=");
            uart_puthex64(_pmm_bitmap_start[i / 8]);
            uart_puts("\n");
#endif
            return (void*)page;
        }
    }
    return 0; // 空きなし
}

void pmm_free(void* addr) {
    uint64_t idx = ((uint64_t)addr) / PAGE_SIZE;
    {
        extern uint64_t g_bfree_elf_watch_phys;
        extern uint64_t g_bfree_shell_text_phys;
        extern int bfree_shell_page_pinned(uint64_t phys);
        uint64_t page = (uint64_t)(uintptr_t)addr & ~(4096ULL - 1ULL);
        if (g_bfree_elf_watch_phys != 0 && page == g_bfree_elf_watch_phys) {
            /* Expected when a vfork+exec child's private AS is destroyed —
             * drop the watch so AS-copy can reuse the frame. */
            g_bfree_elf_watch_phys = 0;
        }
        if ((g_bfree_shell_text_phys != 0 && page == g_bfree_shell_text_phys) ||
            bfree_shell_page_pinned(page)) {
            uart_puts("[PMM] FATAL: refuse free shell text phys=");
            uart_puthex64(page);
            uart_puts("\n");
            return;
        }
    }
    pmm_clear_bit(idx);
    /* Allow pmm_alloc to reuse this page; otherwise the bump cursor only
     * advances and private-AS execve exhausts the pool after ~dozens of loads. */
    if (idx < g_pmm_alloc_start_page) {
        g_pmm_alloc_start_page = idx;
    }
}

void pmm_init_reservations(void) {
    uint64_t reserved_end = KERNEL_RESERVED_LEN;
    uint64_t bitmap_end = (uint64_t)(uintptr_t)_pmm_bitmap_end;
    uint64_t bss_end = (uint64_t)(uintptr_t)_bss_end;
    uint64_t stack_end = (uint64_t)(uintptr_t)_stack_end;
    extern uint8_t _bfree_khi_start[];
    extern uint8_t _bfree_khi_end[];
    uint64_t khi_start = (uint64_t)(uintptr_t)_bfree_khi_start;
    uint64_t khi_end = (uint64_t)(uintptr_t)_bfree_khi_end;

    if (bitmap_end > reserved_end) {
        reserved_end = bitmap_end;
    }
    if (bss_end > reserved_end) {
        reserved_end = bss_end;
    }
    if (stack_end > reserved_end) {
        reserved_end = stack_end;
    }
    if (khi_end > reserved_end) {
        reserved_end = khi_end;
    }
    g_pmm_alloc_start_page = (reserved_end + PAGE_SIZE - 1) / PAGE_SIZE;

    pmm_reserve_range(KERNEL_RESERVED_BASE, KERNEL_RESERVED_LEN, "KERNEL-LOW");
    /* Catch BSS that overflows the legacy 4MiB floor. */
    if (bss_end > KERNEL_RESERVED_LEN) {
        pmm_reserve_range(KERNEL_RESERVED_LEN, bss_end - KERNEL_RESERVED_LEN, "KERNEL-BSS");
    }
    if (khi_end > khi_start) {
        pmm_reserve_range(khi_start, khi_end - khi_start, "KERNEL-HI");
    }
    pmm_reserve_range((uint64_t)(uintptr_t)_pmm_bitmap_start,
        (uint64_t)(uintptr_t)(_pmm_bitmap_end - _pmm_bitmap_start),
        "PMM-BITMAP");
    if (stack_end > (uint64_t)(uintptr_t)_stack_start) {
        pmm_reserve_range((uint64_t)(uintptr_t)_stack_start,
            stack_end - (uint64_t)(uintptr_t)_stack_start, "KSTACK-LNK");
    }
    pmm_reserve_range(APIC_BASE, APIC_LEN, "APIC");
    pmm_reserve_range(IOAPIC_BASE, 0x1000, "IOAPIC");
    pmm_reserve_range(KERNEL_STACK_BASE, KERNEL_STACK_LEN, "KSTACK");
    uart_puts("[PMM] Alloc start=");
    uart_puthex64(g_pmm_alloc_start_page * PAGE_SIZE);
    uart_puts(" bss_end=");
    uart_puthex64(bss_end);
    uart_puts(" khi_end=");
    uart_puthex64(khi_end);
    uart_puts("\n");
}
void knl_dispatch_main(void *regs) {
    (void)regs;
    #if ENABLE_RUNTIME_NET
    extern void net_runtime_poll(void);
    net_runtime_poll();
    #endif
    // 現在のタスクのFPU状態を保存
    knl_save_fpu_state(&knl_current_task->fpu);
    // 次のタスクを選択
    current_task_idx = 1 - current_task_idx;
    knl_current_task = (current_task_idx == 0) ? &tcb1 : &tcb2;
    // --- [VMM] CR3切り替え ---
    uart_puts("[VMM][SWITCH] CR3=");
    uart_puthex64((uint64_t)knl_current_task->page_table_base);
    uart_puts("\n");
    __asm__ volatile ("mov %0, %%cr3" :: "r"(knl_current_task->page_table_base) : "memory");
    // 次のタスクのFPU状態を復元
    knl_restore_fpu_state(&knl_current_task->fpu);
    // CR0.TSをセットし、FPUアクセス時に#NM例外を発生させる
    __asm__ volatile ("mov %%cr0, %%rax; or $(1<<3), %%rax; mov %%rax, %%cr0" ::: "rax", "memory");
    // 次のタスクのエントリを呼び出し（実際のスケジューラではコンテキストスイッチ）
    task_entry[current_task_idx]();
}

// --- システムコールハンドラ extern宣言 ---
extern void syscall_handler(void* frame);


