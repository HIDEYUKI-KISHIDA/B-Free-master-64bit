/* Freestanding guest compositor stub. Not Linux tron_gui_server.
 * Not Wayland protocol yet. PID1 on a cloned ISO (registered as init.elf).
 * Do not use Linux write(1). Do not set BFREE_BOOT_GUI_FIRST on the daily kernel.
 */
#define BFREE_FB0_FD 0x2000

struct fbinfo {
    void *addr;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned char bpp;
    unsigned char pad[3];
    int ready;
};

static long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long rax __asm__("rax") = n;
    register long rdi __asm__("rdi") = a1;
    register long rsi __asm__("rsi") = a2;
    register long rdx __asm__("rdx") = a3;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return rax;
}

static void serial(const char *s, unsigned long n)
{
    (void)sys6(24, (long)s, (long)n, 0, 0, 0, 0);
}

static void serial_hex(const char *label, long v)
{
    char buf[48];
    unsigned long i = 0;
    unsigned long x;
    int sh;
    while (label[i] && i < 24) {
        buf[i] = label[i];
        i++;
    }
    buf[i++] = '0';
    buf[i++] = 'x';
    x = (unsigned long)v;
    for (sh = 60; sh >= 0; sh -= 4) {
        unsigned d = (unsigned)((x >> sh) & 0xFUL);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    buf[i++] = '\n';
    serial(buf, i);
}

void _start(void)
{
    static const char hello[] = "[compositor] guest stub hello\n";
    static const char fillok[] = "[compositor] guest stub fb fill\n";
    struct fbinfo info;
    long mapped;
    unsigned char *fb;
    unsigned y;
    unsigned x;
    unsigned int color = 0x00FF00FFUL; /* magenta: not the FB desk */

    serial(hello, sizeof(hello) - 1);

    info.addr = 0;
    info.pitch = 0;
    info.width = 0;
    info.height = 0;
    info.bpp = 0;
    info.pad[0] = info.pad[1] = info.pad[2] = 0;
    info.ready = 0;
    (void)sys6(1001, (long)&info, 0, 0, 0, 0, 0);
    serial_hex("fb ready=", info.ready);
    serial_hex("fb w=", (long)info.width);
    serial_hex("fb h=", (long)info.height);

    mapped = sys6(26, 0, 0, 3, 1, BFREE_FB0_FD, 0);
    serial_hex("fb mmap=", mapped);
    if (mapped < 0 || info.ready == 0 || info.width == 0 || info.height == 0 ||
        info.pitch == 0) {
        for (;;) {
        }
    }

    fb = (unsigned char *)mapped;
    if (info.bpp >= 24) {
        for (y = 0; y < info.height; y++) {
            unsigned int *row = (unsigned int *)(fb + (unsigned long)y * info.pitch);
            unsigned limit = info.pitch / 4U;
            if (limit > info.width) {
                limit = info.width;
            }
            for (x = 0; x < limit; x++) {
                row[x] = color;
            }
        }
    }
    serial(fillok, sizeof(fillok) - 1);
    for (;;) {
    }
}
