#include "debug_helpers.h"
#include "../include/tk/kernel.h"
#include <stdint.h>
#include <stddef.h>
extern void uart_puts(const char*);
extern void uart_puthex64(uint64_t val);

void debug_dump_regs(const void *pregs, int is_switch) {
    const regs_x86_64_t *regs = (const regs_x86_64_t*)pregs;
    uart_puts(is_switch ? "[DEBUG] TaskSwitch Regs\n" : "[DEBUG] Trap Regs\n");
    uart_puts("RIP="); uart_puthex64(regs->rip); uart_puts(" CS="); uart_puthex64(regs->cs);
    uart_puts(" RFLAGS="); uart_puthex64(regs->rflags); uart_puts("\n");
    uart_puts("RAX="); uart_puthex64(regs->rax); uart_puts(" RBX="); uart_puthex64(regs->rbx);
    uart_puts(" RCX="); uart_puthex64(regs->rcx); uart_puts(" RDX="); uart_puthex64(regs->rdx); uart_puts("\n");
    uart_puts("RSI="); uart_puthex64(regs->rsi); uart_puts(" RDI="); uart_puthex64(regs->rdi);
    uart_puts(" RBP="); uart_puthex64(regs->rbp); uart_puts("\n");
    uart_puts("R8 ="); uart_puthex64(regs->r8); uart_puts(" R9 ="); uart_puthex64(regs->r9);
    uart_puts(" R10="); uart_puthex64(regs->r10); uart_puts(" R11="); uart_puthex64(regs->r11); uart_puts("\n");
    uart_puts("R12="); uart_puthex64(regs->r12); uart_puts(" R13="); uart_puthex64(regs->r13);
    uart_puts(" R14="); uart_puthex64(regs->r14); uart_puts(" R15="); uart_puthex64(regs->r15); uart_puts("\n");
    uart_puts("SS ="); uart_puthex64(regs->ss); uart_puts(" RSP="); uart_puthex64(regs->rsp); uart_puts("\n");
    uart_puts("int_no="); uart_puthex64(regs->int_no); uart_puts(" err_code="); uart_puthex64(regs->err_code); uart_puts("\n");
}
