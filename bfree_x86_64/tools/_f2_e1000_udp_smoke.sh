#!/usr/bin/env bash
# F2: SOCK_DGRAM sendto on 10.0.2/24 goes through e1000 udp_send (not only loopback).
# Guest freestanding smoke: bind + sendto gateway; expect TX path green.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"

make -C kernel ENABLE_RUNTIME_NET=1 -j4 2>&1 | tail -8

cat > /tmp/bfree_f2_udp.c <<'EOF'
/* Freestanding: UDP sendto 10.0.2.2 via kernel e1000 path. */
#include <stdint.h>
#define SYS_write 1
#define SYS_socket 41
#define SYS_bind 49
#define SYS_sendto 44
#define SYS_exit 60
#define AF_INET 2
#define SOCK_DGRAM 2
struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t pad[8];
};
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
static uint16_t htons16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static uint32_t htonl32(uint32_t x)
{
    return ((x & 0xffU) << 24) | ((x & 0xff00U) << 8) |
           ((x >> 8) & 0xff00U) | (x >> 24);
}
void _start(void)
{
    long fd;
    struct sockaddr_in local, dst;
    long wr;
    fd = sys6(SYS_socket, AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
    if (fd < 0) { ser("[f2] FAIL socket\n"); sys6(SYS_exit, 1, 0, 0, 0, 0, 0); }
    local.sin_family = AF_INET;
    local.sin_port = htons16(18080);
    local.sin_addr = 0; /* INADDR_ANY */
    if (sys6(SYS_bind, fd, (long)&local, 16, 0, 0, 0) != 0) {
        ser("[f2] FAIL bind\n"); sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
    }
    dst.sin_family = AF_INET;
    dst.sin_port = htons16(9); /* discard */
    dst.sin_addr = htonl32(0x0a000202U); /* 10.0.2.2 gateway */
    wr = sys6(SYS_sendto, fd, (long)"F2E1", 4, 0, (long)&dst, 16);
    if (wr != 4) {
        ser("[f2] FAIL sendto\n");
        sys6(SYS_exit, 3, 0, 0, 0, 0, 0);
    }
    ser("[f2] F2_E1000_TX_OK\n");
    for (;;) { __asm__ volatile("pause"); }
}
EOF

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/f2_udp_smoke.elf /tmp/bfree_f2_udp.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

ISO_STAGE=/tmp/bfree-f2-iso
ISO=/tmp/bfree-f2.iso
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/f2_udp_smoke.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkf2.log 2>&1

QLOG=/tmp/bfree-f2.log
: > "$QLOG"
(
  sleep 35
  printf '\n'
  sleep 1
) | timeout 50 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
    -netdev user,id=n0 -device e1000,netdev=n0 \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo '=== F2 e1000 UDP ==='
fail=0
if grep -aq 'F2_E1000_TX_OK' "$QLOG"; then
  echo 'PASS F2_E1000_TX_OK'
else
  echo 'FAIL F2_E1000_TX_OK'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[f2\]|NETDRV|NET\]|e1000|F2_|PANIC' "$QLOG" | tail -25
exit "$fail"
