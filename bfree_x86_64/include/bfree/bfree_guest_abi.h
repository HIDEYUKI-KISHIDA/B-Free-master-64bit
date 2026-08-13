#ifndef BFREE_GUEST_ABI_H
#define BFREE_GUEST_ABI_H

#include <stdint.h>

/* B-Free compositor / legacy guest syscall numbers (see userland/libc/bfree_epoll.h). */
#define BFREE_SYS_POLL_INPUT_EVENT        0
#define BFREE_SYS_GET_FRAMEBUFFER_INFO    1
#define BFREE_SYS_GET_FRAMEBUFFER_INFO_LEGACY 1001
#define BFREE_SYS_CLEAR_SCREEN            2
#define BFREE_SYS_GET_TIME                3
#define BFREE_SYS_INPUT_EVENT_PENDING     4
#define BFREE_SYS_FBDEV_IOCTL             20
#define BFREE_SYS_INPUT_IOCTL             21
#define BFREE_SYS_IOCTL                   22
#define BFREE_SYS_DEBUG_SERIAL_WRITE      24
#define BFREE_SYS_PIPE                    25
#define BFREE_SYS_MMAP                    26
#define BFREE_SYS_SHM_OPEN                27
#define BFREE_SYS_CLOCK_GETTIME           29

#define BFREE_FB0_FD                      0x2000
#define BFREE_FB0_USER_MMAP_BASE          0x01400000u

#define TK2GPU_IOCTL_BASE                 0x1000
#define TK2GPU_IOCTL_GET_INFO             (TK2GPU_IOCTL_BASE + 1)

typedef struct {
    void *addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    int ready;
} bfree_framebuffer_info_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t phys_addr;
    uint64_t size;
} tk2gpu_fbinfo_t;

static inline long bfree_guest_syscall1(long nr, long a1)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long bfree_guest_syscall2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long bfree_guest_syscall3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long bfree_guest_syscall5(long nr, long a1, long a2, long a3, long a4, long a5)
{
    long ret;
    register long r8 asm("r8") = a4;
    register long r9 asm("r9") = a5;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

#endif /* BFREE_GUEST_ABI_H */
