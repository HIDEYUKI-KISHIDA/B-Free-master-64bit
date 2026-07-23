#!/usr/bin/env bash
# F2 TCP: guest connect(10.0.2.2:PORT) → host echo via QEMU slirp NAT; PING→PONG.
# Prefer gateway 10.0.2.2 (host) over guestfwd: guestfwd often accepts on host
# without delivering SYN-ACK to a minimal guest TCP stack.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"
PORT="${BFREE_F2_TCP_PORT:-18081}"
GUEST_DST="${BFREE_F2_TCP_DST:-10.0.2.2}"
GUEST_DST_HEX=0x0a000202  # 10.0.2.2
if [ "$GUEST_DST" != "10.0.2.2" ]; then
  echo "set GUEST_DST_HEX for $GUEST_DST" >&2
  exit 1
fi

touch kernel/sysmain/main.c
make -C kernel ENABLE_RUNTIME_NET=1 -j4 2>&1 | tail -20

cat > /tmp/bfree_f2_tcp.c <<EOF
#include <stdint.h>
#define SYS_write 1
#define SYS_read 0
#define SYS_socket 41
#define SYS_connect 42
#define SYS_sendto 44
#define SYS_recvfrom 45
#define SYS_exit 60
#define AF_INET 2
#define SOCK_STREAM 1
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
static int eq4(const char *a, const char *b)
{
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}
void _start(void)
{
    long fd, n, wr, rc;
    struct sockaddr_in dst;
    char buf[64];
    int spins;

    fd = sys6(SYS_socket, AF_INET, SOCK_STREAM, 0, 0, 0, 0);
    if (fd < 0) { ser("[f2tcp] FAIL socket\\n"); sys6(SYS_exit, 1, 0, 0, 0, 0, 0); }
    dst.sin_family = AF_INET;
    dst.sin_port = htons16(${PORT});
    dst.sin_addr = htonl32(${GUEST_DST_HEX}U); /* ${GUEST_DST} slirp host/gateway */
    /* Non-blocking connect: SYN sent, handshake completes across write/read syscalls. */
    rc = sys6(SYS_connect, fd, (long)&dst, 16, 0, 0, 0);
    if (rc != 0 && rc != -115) {
        ser("[f2tcp] FAIL connect\\n");
        for (;;) { __asm__ volatile("pause"); }
    }
    ser("[f2tcp] CONNECT_OK\\n");
    wr = -115;
    for (spins = 0; spins < 400000; ++spins) {
        wr = sys6(SYS_write, fd, (long)"PING", 4, 0, 0, 0);
        if (wr == 4) break;
        if (wr != -115 && wr != -11) break;
    }
    if (wr != 4) {
        ser("[f2tcp] FAIL write\\n");
        sys6(SYS_exit, 3, 0, 0, 0, 0, 0);
    }
    for (spins = 0; spins < 200000; ++spins) {
        n = sys6(SYS_read, fd, (long)buf, 64, 0, 0, 0);
        if (n == -115 || n == -11) continue;
        if (n >= 4 && eq4(buf, "PONG")) {
            ser("[f2tcp] F2_E1000_TCP_OK\\n");
            for (;;) { __asm__ volatile("pause"); }
        }
        if (n < 0) break;
    }
    ser("[f2tcp] FAIL recv\\n");
    sys6(SYS_exit, 4, 0, 0, 0, 0, 0);
}
EOF

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/f2_tcp_smoke.elf /tmp/bfree_f2_tcp.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

ISO_STAGE=/tmp/bfree-f2tcp-iso
ISO=/tmp/bfree-f2tcp.iso
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/f2_tcp_smoke.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkf2tcp.log 2>&1

# Host echo on all interfaces: slirp maps guest 10.0.2.2:PORT → host :PORT.
python3 - "$PORT" <<'PY' &
import socket, sys, time
port = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
s.listen(1)
s.settimeout(90)
print("HOST_LISTEN", port, flush=True)
try:
    c, addr = s.accept()
    print("HOST_ACCEPT", addr, flush=True)
    data = b""
    while len(data) < 4:
        chunk = c.recv(64)
        if not chunk:
            break
        data += chunk
    if data[:4] == b"PING":
        c.sendall(b"PONG")
        print("HOST_ECHO_OK", flush=True)
    else:
        print("HOST_BAD", data, flush=True)
    c.close()
except Exception as e:
    print("HOST_ECHO_FAIL", e, flush=True)
finally:
    s.close()
PY
HOSTPID=$!
sleep 1

QLOG=/tmp/bfree-f2tcp.log
: > "$QLOG"
timeout 70 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
    -netdev user,id=n0 \
    -device e1000,netdev=n0 \
    -object filter-dump,id=f0,netdev=n0,file=/tmp/bfree-f2tcp.pcap \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

wait "$HOSTPID" 2>/dev/null || true
if command -v tcpdump >/dev/null 2>&1 && [ -f /tmp/bfree-f2tcp.pcap ]; then
  echo '=== pcap (tcp) ==='
  tcpdump -nn -r /tmp/bfree-f2tcp.pcap 2>/dev/null | head -40 || true
fi

echo '=== F2 e1000 TCP ==='
fail=0
if grep -aq 'CONNECT_OK' "$QLOG"; then
  echo 'PASS CONNECT_OK'
else
  echo 'FAIL CONNECT_OK'
  fail=1
fi
if grep -aq 'F2_E1000_TCP_OK' "$QLOG"; then
  echo 'PASS F2_E1000_TCP_OK'
else
  echo 'FAIL F2_E1000_TCP_OK'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[f2tcp\]|\[TCP\]|\[NET\]|F2_|PANIC|Page Fault' "$QLOG" | tail -40
exit "$fail"
