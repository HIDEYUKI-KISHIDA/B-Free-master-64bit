#ifdef __x86_64__
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code; // 割り込み番号・エラーコード（必要に応じて）
    uint64_t rip, cs, rflags, rsp, ss;
} regs_x86_64_t;
#endif
#ifndef TK_KERNEL_H
#define TK_KERNEL_H

#include <stdint.h>
#include "tk/fpu_state.h"

void uart_puthex64(uint64_t val);


typedef struct {
    struct {
        void *ssp;
        uint64_t regs[16]; // RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8-R15
        uint64_t rip;
        uint64_t rflags;
        uint64_t cs, ss;
    } tskctxb;
    fpu_state_t fpu;
    void *kernel_stack_base;
    void *page_table_base; // CR3
    /* Ring3 stack high edge (one byte past last mapped stack byte), set at boot in setup_task_stack. */
    uint64_t user_stack_top;
    /* musl/Qt TLS: ARCH_SET_FS (MSR 0xC0000100); restored across syscalls. */
    uint64_t user_fsbase;
} TCB;

extern TCB *knl_current_task;
extern void knl_dispatch_main(void *regs);
extern uint8_t knl_kernel_stack_top[];

#endif // TK_KERNEL_H
