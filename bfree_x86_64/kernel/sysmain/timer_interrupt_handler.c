#include <stdint.h>
#include "../include/tk/kernel.h"
#include "interrupt_main.h"
#include "keyboard.h"
#include <stddef.h>
#include "debug_helpers.h"

extern void knl_dispatch_main(void *regs);
extern TCB *knl_current_task;
extern int current_task_idx;
extern void uart_puts(const char*);

// タイマ割り込みCハンドラ
void timer_interrupt_handler_c(regs_x86_64_t *regs) {
    // デバッグダンプ・タスクスイッチは一旦無効化（userland起動後のちらつき防止）
    (void)regs;
}
