#!/usr/bin/env bash
# pthread_create via CLONE_THREAD: handshake park → parent returns → child runs → join.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"

make -C kernel -j4 2>&1 | tail -5

cat > /tmp/bfree_pthread_clone_smoke.c <<'EOF'
#include <stdint.h>
#define SYS_write 1
#define SYS_close 3
#define SYS_clone 56
#define SYS_exit 60
#define SYS_futex 202
#define SYS_ppoll 271
#define SYS_eventfd2 290
#define CLONE_VM 0x00000100UL
#define CLONE_FILES 0x00000400UL
#define CLONE_SIGHAND 0x00000800UL
#define CLONE_THREAD 0x00010000UL
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
struct pollfd { int fd; short events; short revents; };
static long sys6(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}
static void ser(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    (void)sys6(SYS_write, 1, (long)s, (long)(p - s), 0, 0, 0);
}
static uint8_t g_stack[64 * 1024] __attribute__((aligned(16)));
static volatile int g_done;
static volatile int g_child_ran;
static int g_gate_efd = -1;
static void *(*g_fn)(void *);
static void *g_arg;
static void *g_result;

static void __attribute__((noreturn)) child_entry(void)
{
    struct pollfd pfd;
    pfd.fd = g_gate_efd;
    pfd.events = 0x001;
    pfd.revents = 0;
    (void)sys6(SYS_ppoll, (long)&pfd, 1, 0, 0, 0, 0);
    g_child_ran = 1;
    if (g_fn)
        g_result = g_fn(g_arg);
    g_done = 1;
    (void)sys6(SYS_futex, (long)&g_done, FUTEX_WAKE, 1, 0, 0, 0);
    sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;) __asm__ volatile("pause");
}

static long clone_thread(void *sp, void *fn)
{
    long ret;
    unsigned long flags = CLONE_VM | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD;
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
        : [flags] "r"(flags), [stack] "r"(sp), [fn] "r"(fn)
        : "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory");
    return ret;
}

static void *worker(void *arg)
{
    (void)arg;
    ser("[pt] worker\n");
    return (void *)(uintptr_t)0x42;
}

void _start(void)
{
    void *sp = g_stack + sizeof(g_stack);
    long tid;
    uint64_t one = 1;

    ser("[pt] begin\n");
    g_gate_efd = (int)sys6(SYS_eventfd2, 0, 0, 0, 0, 0, 0);
    if (g_gate_efd < 0) {
        ser("[pt] FAIL eventfd\n");
        sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
    }
    g_fn = worker;
    g_arg = 0;
    g_done = 0;
    g_child_ran = 0;
    sp = (void *)(((uintptr_t)sp) & ~(uintptr_t)0xFULL);
    tid = clone_thread(sp, (void *)child_entry);
    if (tid < 0) {
        ser("[pt] FAIL clone\n");
        sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
    }
    ser("[pt] parent after clone\n");
    (void)sys6(SYS_write, g_gate_efd, (long)&one, 8, 0, 0, 0);
    while (!g_done) {
        (void)sys6(SYS_futex, (long)&g_done, FUTEX_WAIT, 0, 0, 0, 0);
    }
    (void)sys6(SYS_close, g_gate_efd, 0, 0, 0, 0, 0);
    if (g_child_ran && g_result == (void *)(uintptr_t)0x42) {
        ser("[pt] PASS\n");
    } else {
        ser("[pt] FAIL join\n");
    }
    for (;;) __asm__ volatile("pause");
}
EOF

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/pthread_clone_smoke.elf /tmp/bfree_pthread_clone_smoke.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

ISO_STAGE=/tmp/bfree-pt-iso
ISO=/tmp/bfree-pt.iso
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/pthread_clone_smoke.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkpt.log 2>&1

QLOG=/tmp/bfree-pt.log
: > "$QLOG"
timeout 45 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo '=== pthread clone smoke ==='
fail=0
if grep -aq '\[pt\] PASS' "$QLOG"; then
  echo 'PASS pthread_clone'
else
  echo 'FAIL pthread_clone'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[pt\]|PANIC|Page Fault' "$QLOG" | tail -20
exit "$fail"
