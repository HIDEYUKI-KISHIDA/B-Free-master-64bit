// --- クロスビルド用ダミー定義 ---
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef E_OK
#define E_OK 0
#endif
#ifndef E_DEV
#define E_DEV -3
#endif
#ifndef DMA_READ
#define DMA_READ 0
#endif
#ifndef DMA_MASK
#define DMA_MASK 0
#endif
#ifndef ROUNDUP
#define ROUNDUP(x,align) (((x + (align - 1)) / align) * align)
#endif
extern void busywait(int x);
extern void wait_int(int *x);
extern void setup_dma(void *a, int b, int c, int d);
extern void lock(void);
extern void unlock(void);

#include "interrupt_handler.h"
#include "lib.h"
#include "scheduler.h"

/* グローバルハンドラテーブル */
irq_handler_t irq_table[NUM_IRQ];

/* PIC ポート定義 */
#define PIC_MASTER_CMD      0x20
#define PIC_MASTER_DATA     0x21
#define PIC_SLAVE_CMD       0xA0
#define PIC_SLAVE_DATA      0xA1
#define PIC_EOI             0x20

/* 割り込みハンドラテーブル初期化 */
int interrupt_handler_init(void)
{
    int i;
    
    /* 全てのハンドラをデフォルトに設定 */
    for (i = 0; i < NUM_IRQ; i++) {
        irq_table[i] = default_irq_handler;
    }
    
    /* 例外ハンドラを登録 */
    irq_table[EX_DIVIDE] = default_exception_handler;
    irq_table[EX_DEBUG] = default_exception_handler;
    irq_table[EX_BREAKPOINT] = default_exception_handler;
    irq_table[EX_OVERFLOW] = default_exception_handler;
    irq_table[EX_BOUNDS] = default_exception_handler;
    irq_table[EX_OPCODE] = default_exception_handler;
    irq_table[EX_PAGE_FAULT] = page_fault_handler;
    irq_table[EX_GPF] = gpf_handler;
    
    /* デバイス割り込みを登録 */
    irq_table[IRQ_TIMER] = timer_irq_handler;
    irq_table[IRQ_KEYBOARD] = keyboard_irq_handler;
    
    /* PIC初期化 */
    pic_init();
    
    console_printf("Interrupt handlers initialized\n");
    
    return 0;
}

/* IRQハンドラを登録 */
int register_irq_handler(int irq, irq_handler_t handler)
{
    if (irq < 0 || irq >= NUM_IRQ || !handler) {
        return -1;
    }
    
    irq_table[irq] = handler;
    console_printf("IRQ handler registered: IRQ %d\n", irq);
    
    return 0;
}

/* IRQハンドラを解除 */
int unregister_irq_handler(int irq)
{
    if (irq < 0 || irq >= NUM_IRQ) {
        return -1;
    }
    
    irq_table[irq] = default_irq_handler;
    
    return 0;
}

/* IRQハンドラを取得 */
irq_handler_t get_irq_handler(int irq)
{
    if (irq < 0 || irq >= NUM_IRQ) {
        return NULL;
    }
    
    return irq_table[irq];
}

/* デフォルトIRQハンドラ */
void default_irq_handler(struct regs *regs)
{
    int irq = regs->rax & 0xFF;
    console_printf("Unhandled IRQ %d\n", irq);
    pic_send_eoi(irq);
}

/* デフォルト例外ハンドラ */
void default_exception_handler(struct regs *regs)
{
    unsigned long error_code = regs->rbx;
    
    console_printf("Exception occurred!\n");
    console_printf("RIP: 0x%016lx\n", regs->rip);
    console_printf("RSP: 0x%016lx\n", regs->rsp);
    console_printf("RBP: 0x%016lx\n", regs->rbp);
    console_printf("Error code: 0x%016lx\n", error_code);
    
    /* ハング */
    while (1) asm("hlt");
}

/* ページフォルトハンドラ */
void page_fault_handler(struct regs *regs)
{
    unsigned long cr2;
    unsigned long error_code = regs->rbx;
    
    /* CR2レジスタ（フォルト線形アドレス）を読む */
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    
    console_printf("Page Fault!\n");
    console_printf("Address: 0x%016lx\n", cr2);
    console_printf("Error code: 0x%016lx\n", error_code);
    
    if (error_code & 0x1) {
        console_printf("  Protection violation\n");
    } else {
        console_printf("  Page not present\n");
    }
    
    if (error_code & 0x2) {
        console_printf("  Write access\n");
    } else {
        console_printf("  Read access\n");
    }
    
    if (error_code & 0x4) {
        console_printf("  User mode\n");
    } else {
        console_printf("  Kernel mode\n");
    }
    
    /* ハング */
    while (1) asm("hlt");
}

/* 一般保護例外ハンドラ */
void gpf_handler(struct regs *regs)
{
    unsigned long error_code = regs->rbx;
    
    console_printf("General Protection Fault!\n");
    console_printf("RIP: 0x%016lx\n", regs->rip);
    console_printf("Error code: 0x%016lx\n", error_code);
    
    if (current_proc) {
        console_printf("Process: PID=%d\n", current_proc->pid);
    }
    
    /* ハング */
    while (1) asm("hlt");
}

/* タイマーIRQハンドラ */
void timer_irq_handler(struct regs *regs)
{
    /* スケジューラのタイマーティック処理 */
    timer_tick();
    
    /* PIC に EOI を送信 */
    pic_send_eoi(IRQ_TIMER);
}

/* キーボードIRQハンドラ */
void keyboard_irq_handler(struct regs *regs)
{
    unsigned char scancode;
    
    /* キーボードコントローラからスキャンコードを読み込む */
    scancode = inb(0x60);
    
    console_printf("Keyboard: scancode=0x%02x\n", scancode);
    
    /* PIC に EOI を送信 */
    pic_send_eoi(IRQ_KEYBOARD);
}

/* PIC（8259A）初期化 */
void pic_init(void)
{
    /* マスターPIC ICW1 */
    outb(0x20, 0x11);  /* ICW1: ICW4が必要、エッジトリガ */
    
    /* マスターPIC ICW2: ベクトルアドレス */
    outb(0x21, 0x20);  /* マスター: ベクトル 0x20 */
    
    /* マスターPIC ICW3: スレーブ接続 */
    outb(0x21, 0x04);  /* IRQ2がスレーブ */
    
    /* マスターPIC ICW4 */
    outb(0x21, 0x01);  /* 8086モード */
    
    /* マスターPIC IMR: 全割り込み許可（とりあえず） */
    outb(0x21, 0x00);  /* 0x00: 全IRQ許可、0xFB: IRQ2除外 */
    
    /* スレーブPIC ICW1 */
    outb(0xA0, 0x11);
    
    /* スレーブPIC ICW2: ベクトルアドレス */
    outb(0xA1, 0x28);  /* スレーブ: ベクトル 0x28 */
    
    /* スレーブPIC ICW3: マスターへの接続 */
    outb(0xA1, 0x02);  /* マスターの IRQ2 に接続 */
    
    /* スレーブPIC ICW4 */
    outb(0xA1, 0x01);  /* 8086モード */
    
    /* スレーブPIC IMR: 全割り込み許可 */
    outb(0xA1, 0x00);
    
    console_printf("PIC initialized\n");
}

/* PIC に EOI（End of Interrupt）を送信 */
void pic_send_eoi(int irq)
{
    if (irq >= 8) {
        /* スレーブPICからの割り込み */
        outb(0xA0, 0x20);  /* スレーブに EOI */
    }
    
    /* マスターPICに EOI */
    outb(0x20, 0x20);
}

/* 特定のIRQを無効にする */
void pic_disable_irq(int irq)
{
    unsigned char mask;
    int port;
    
    if (irq < 8) {
        port = 0x21;  /* マスターPIC IMR */
    } else {
        port = 0xA1;  /* スレーブPIC IMR */
        irq -= 8;
    }
    
    mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}

/* 特定のIRQを有効にする */
void pic_enable_irq(int irq)
{
    unsigned char mask;
    int port;
    
    if (irq < 8) {
        port = 0x21;  /* マスターPIC IMR */
    } else {
        port = 0xA1;  /* スレーブPIC IMR */
        irq -= 8;
    }
    
    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}
