
#include <stdint.h>
#include "sysdepend/x86_64/uart.h"
#include "../fbdev.h"
#include "../gpu_backend.h"
#include "../../userland/libc/bfree_epoll.h"

#define SYSCALL_SERIAL_WRITE 0
#define SYSCALL_GET_FRAMEBUFFER_INFO 1
#define SYSCALL_GET_FRAMEBUFFER_INFO_LEGACY 1001

typedef struct {
    void *addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    int ready;
} bfree_framebuffer_info_t;

__attribute__((interrupt)) void syscall_handler(void* frame) {
    uint64_t num, arg1;
    (void)frame;
    __asm__ volatile ("mov %%rax, %0" : "=r"(num));
    __asm__ volatile ("mov %%rdi, %0" : "=r"(arg1));
    if (num == SYSCALL_SERIAL_WRITE) {
        uart_puts((const char*)arg1);
        return;
    }

    if (num == SYSCALL_GET_FRAMEBUFFER_INFO || num == SYSCALL_GET_FRAMEBUFFER_INFO_LEGACY) {
        bfree_framebuffer_info_t *out = (bfree_framebuffer_info_t *)arg1;
        tk2gpu_fbinfo_t fbinfo;

        if (out == 0) {
            return;
        }

        if (runtime_fbdev_ioctl(TK2GPU_IOCTL_GET_INFO, &fbinfo) != 0) {
            out->addr = 0;
            out->pitch = 0;
            out->width = 0;
            out->height = 0;
            out->bpp = 0;
            out->ready = 0;
            return;
        }

        out->addr = (void *)(uintptr_t)BFREE_FB0_USER_MMAP_BASE;
        out->pitch = fbinfo.pitch;
        out->width = fbinfo.width;
        out->height = fbinfo.height;
        out->bpp = (uint8_t)fbinfo.bpp;
        out->ready = 1;
    }
}
