#ifndef __INTERRUPT_HANDLER_H__
#define __INTERRUPT_HANDLER_H__

#include "types.h"
#include "process.h"

/* 割り込みベクトル数 */
#define NUM_IRQ         256

/* デバイス割り込み */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_SLAVE8259   2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_RTCLOCK     8
#define IRQ_REDIRECT    9
#define IRQ_RESERVED1   10
#define IRQ_RESERVED2   11
#define IRQ_MOUSE       12
#define IRQ_FPU         13
#define IRQ_IDE1        14
#define IRQ_IDE2        15

/* 例外ベクトル */
#define EX_DIVIDE       0
#define EX_DEBUG        1
#define EX_NMI          2
#define EX_BREAKPOINT   3
#define EX_OVERFLOW     4
#define EX_BOUNDS       5
#define EX_OPCODE       6
#define EX_FPU          7
#define EX_DOUBLE_FAULT 8
#define EX_TSS_FAULT    10
#define EX_SEGMENT      11
#define EX_STACK        12
#define EX_GPF          13
#define EX_PAGE_FAULT   14
#define EX_FPU_ERROR    16
#define EX_ALIGNMENT    17
#define EX_MACHINE      18

/* 割り込みハンドラ関数ポインタ */
typedef void (*irq_handler_t)(struct regs *regs);

/* グローバル変数 */
extern irq_handler_t irq_table[NUM_IRQ];

/* 割り込みハンドラ登録 */
int register_irq_handler(int irq, irq_handler_t handler);
int unregister_irq_handler(int irq);
irq_handler_t get_irq_handler(int irq);

/* 割り込み初期化 */
int interrupt_handler_init(void);

/* デフォルトハンドラ */
void default_irq_handler(struct regs *regs);
void default_exception_handler(struct regs *regs);

/* タイマー割り込み */
void timer_irq_handler(struct regs *regs);

/* キーボード割り込み */
void keyboard_irq_handler(struct regs *regs);

/* ページフォルト処理 */
void page_fault_handler(struct regs *regs);

/* 一般保護例外 */
void gpf_handler(struct regs *regs);

/* PIC（Programmable Interrupt Controller）操作 */
void pic_init(void);
void pic_send_eoi(int irq);
void pic_disable_irq(int irq);
void pic_enable_irq(int irq);

#endif  /* __INTERRUPT_HANDLER_H__ */
