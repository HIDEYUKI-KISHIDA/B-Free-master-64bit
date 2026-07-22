/* Guest smoke: CLONE_THREAD + eventfd + ppoll (kernel cooperative schedule). */
#include <stdint.h>

#define SYS_write 1
#define SYS_clone 56
#define SYS_exit 60
#define SYS_ppoll 271
#define SYS_eventfd2 290

#define CLONE_VM 0x00000100UL
#define CLONE_FILES 0x00000400UL
#define CLONE_SIGHAND 0x00000800UL
#define CLONE_THREAD 0x00010000UL

struct pollfd {
    int fd;
    short events;
    short revents;
};

static long bfree_syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static long bfree_syscall3(long n, long a, long b, long c)
{
    return bfree_syscall6(n, a, b, c, 0, 0, 0);
}

static void ser(const char *s)
{
    const char *p = s;
    while (*p)
        ++p;
    (void)bfree_syscall3(SYS_write, 1, (long)s, (long)(p - s));
}

static uint8_t g_stack[64 * 1024] __attribute__((aligned(16)));
static volatile int g_child_ran;
static volatile int g_child_woken;
static volatile int g_parent_resumed;
static int g_efd = -1;

static void __attribute__((noreturn)) child_main(void)
{
    struct pollfd pfd;

    g_child_ran = 1;
    ser("[gthr] child ppoll\n");
    pfd.fd = g_efd;
    pfd.events = 0x001;
    pfd.revents = 0;
    (void)bfree_syscall6(SYS_ppoll, (long)&pfd, 1, 0, 0, 0, 0);
    g_child_woken = 1;
    ser("[gthr] child woken\n");
    bfree_syscall3(SYS_exit, 0, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}

/* clone; if child (rax==0) jump to child_main on new stack (already set by kernel).
 * Use fixed regs so mov $56,%rax cannot clobber newsp/flags (was → EFAULT). */
static long clone_thread(void *child_sp)
{
    long ret;
    unsigned long flags = CLONE_VM | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD;
    void *fn = (void *)child_main;

    __asm__ volatile(
        "mov %[flags], %%rdi\n\t"
        "mov %[stack], %%rsi\n\t"
        "mov %[fn], %%r9\n\t"
        "mov $56, %%rax\n\t"
        "xor %%rdx, %%rdx\n\t"
        "xor %%r10, %%r10\n\t"
        "xor %%r8, %%r8\n\t"
        "syscall\n\t"
        "test %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "xor %%rbp, %%rbp\n\t"
        "jmp *%%r9\n\t"
        "1:\n\t"
        : "=a"(ret)
        : [flags] "r"(flags), [stack] "r"(child_sp), [fn] "r"(fn)
        : "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory");
    return ret;
}

void _start(void)
{
    long tid;
    void *sp = g_stack + sizeof(g_stack);

    ser("[gthr] begin\n");
    g_efd = (int)bfree_syscall3(SYS_eventfd2, 0, 0, 0);
    if (g_efd < 0) {
        char buf[64];
        long r = (long)g_efd;
        int i = 0;
        buf[i++] = '[';
        buf[i++] = 'g';
        buf[i++] = 't';
        buf[i++] = 'h';
        buf[i++] = 'r';
        buf[i++] = ']';
        buf[i++] = ' ';
        buf[i++] = 'e';
        buf[i++] = 'f';
        buf[i++] = 'd';
        buf[i++] = '=';
        /* print signed hex */
        if (r < 0) {
            buf[i++] = '-';
            r = -r;
        }
        buf[i++] = '0';
        buf[i++] = 'x';
        for (int s = 28; s >= 0; s -= 4) {
            int d = (int)((r >> s) & 0xf);
            buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        }
        buf[i++] = '\n';
        buf[i] = 0;
        ser(buf);
        bfree_syscall3(SYS_exit, 1, 0, 0);
    }

    sp = (void *)(((uintptr_t)sp) & ~(uintptr_t)0xFULL);
    tid = clone_thread(sp);
    if (tid < 0) {
        ser("[gthr] FAIL clone\n");
        bfree_syscall3(SYS_exit, 2, 0, 0);
    }
    g_parent_resumed = 1;
    ser("[gthr] parent after clone\n");
    {
        uint64_t one = 1;
        long wr = bfree_syscall3(SYS_write, g_efd, (long)&one, 8);
        if (wr < 0) {
            ser("[gthr] write fail\n");
        }
    }
    ser("[gthr] parent wrote eventfd\n");
    for (volatile int i = 0; i < 5000000; ++i) {
        if (g_child_woken) {
            break;
        }
    }
    if (g_child_ran && g_parent_resumed && g_child_woken) {
        ser("[gthr] PASS\n");
        for (;;) {
            __asm__ volatile("pause");
        }
    }
    if (!g_child_woken) {
        ser("[gthr] FAIL no wake\n");
    } else {
        ser("[gthr] FAIL flags\n");
    }
    for (;;) {
        __asm__ volatile("pause");
    }
}
