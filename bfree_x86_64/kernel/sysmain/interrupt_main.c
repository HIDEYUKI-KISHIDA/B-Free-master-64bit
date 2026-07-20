#include <stdint.h>
#include "keyboard.h"
extern void uart_puts(const char*);
extern void uart_putc(char c);
extern void uart_puthex64(uint64_t);
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
extern void kputc(char c);

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

volatile uint8_t knl_dispatch_request = 0;

// outb宣言（kernel_main.cと重複する場合はexternでOK）

// irq_handlers配列の外部参照
extern void (*irq_handlers[256])(void *regs);

static void dump_exception_frame(uint64_t vecno, uint64_t *frame)
{
    uint64_t errcode = frame ? frame[15] : 0;
    uint64_t rip = frame ? frame[16] : 0;
    uint64_t cs = frame ? frame[17] : 0;
    uint64_t rflags = frame ? frame[18] : 0;
    uint64_t rsp_user = 0;
    uint64_t ss_user = 0;
    uint64_t cr0 = 0;
    uint64_t cr4 = 0;
    uint64_t fsbase = 0;

    uart_puts("[PANIC][STAGE=exception] vector=");
    uart_puthex64(vecno);
    uart_puts(" errcode=");
    uart_puthex64(errcode);
    uart_puts("\n");

    if (!frame) {
        uart_puts("[PANIC][STAGE=exception] frame unavailable\n");
        return;
    }

    uart_puts("[PANIC][CTX] RIP="); uart_puthex64(rip);
    uart_puts(" CS=");              uart_puthex64(cs);
    uart_puts(" RFLAGS=");          uart_puthex64(rflags);
    uart_puts("\n");

    uart_puts("[PANIC][CTX] RAX="); uart_puthex64(frame[0]);
    uart_puts(" RBX=");             uart_puthex64(frame[1]);
    uart_puts(" RCX=");             uart_puthex64(frame[2]);
    uart_puts(" RDX=");             uart_puthex64(frame[3]);
    uart_puts("\n");

    uart_puts("[PANIC][CTX] RSI="); uart_puthex64(frame[4]);
    uart_puts(" RDI=");             uart_puthex64(frame[5]);
    uart_puts(" RBP=");             uart_puthex64(frame[14]);
    uart_puts("\n");

    if ((cs & 0x3U) == 0x3U) {
        rsp_user = frame[19];
        ss_user = frame[20];
        uart_puts("[PANIC][CTX] RSP(user)="); uart_puthex64(rsp_user);
        uart_puts(" SS(user)=");              uart_puthex64(ss_user);
        uart_puts("\n");
    }

    {
        uint32_t fs_lo = 0;
        uint32_t fs_hi = 0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
        __asm__ volatile ("rdmsr" : "=a"(fs_lo), "=d"(fs_hi) : "c"(0xC0000100u));
        fsbase = ((uint64_t)fs_hi << 32) | (uint64_t)fs_lo;
    }
    uart_puts("[PANIC][CTX] CR0="); uart_puthex64(cr0);
    uart_puts(" CR4="); uart_puthex64(cr4);
    uart_puts(" FS_BASE="); uart_puthex64(fsbase);
    uart_puts("\n");

    /* Avoid SMAP #PF: only touch user RIP when SMAP is off. */
    if ((cs & 0x3U) == 0x3U && rip < 0x0000800000000000ULL &&
        (cr4 & (1ULL << 21)) == 0ULL) {
        const volatile unsigned char *p =
            (const volatile unsigned char *)(uintptr_t)rip;
        int bi;
        uart_puts("[PANIC][CTX] bytes@RIP=");
        for (bi = 0; bi < 8; ++bi) {
            uart_puthex64((uint64_t)p[bi]);
            uart_puts(bi + 1 < 8 ? " " : "\n");
        }
    }

    if (vecno == 14U) {
        uint64_t cr2 = 0;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        uart_puts("[PANIC][CTX] CR2="); uart_puthex64(cr2); uart_puts("\n");
    }
}

void knl_interrupt_main(uint64_t *regs, uint64_t vecno) {
    // 割り込みディスパッチ
    if (vecno < 256 && irq_handlers[vecno]) {
        irq_handlers[vecno](regs);
    } else if (vecno < 32) {
        uint64_t *frame = 0;
        if (regs) {
            /* regs[0] は SAVE_ALL フレーム先頭 (RAX位置) へのポインタ */
            frame = (uint64_t *)regs[0];
        }
        dump_exception_frame(vecno, frame);
        while (1) {
            __asm__ volatile ("cli; hlt");
        }
    }
    // PICにEOI送信
    if (vecno >= 32 && vecno <= 39) {
        outb(0x20, 0x20);
    } else if (vecno >= 40 && vecno <= 47) {
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}
