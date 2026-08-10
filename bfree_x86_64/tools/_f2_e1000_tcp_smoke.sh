#!/usr/bin/env bash
# F2 TCP (Max3 deepen): guest connect(10.0.2.2:PORT) → host echo via QEMU slirp.
# Three RTTs on one connection: PING→PONG, PNG2→PONG, then 16B PAY3→PONG16.
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
static int eqn(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}
static long write_all(long fd, const char *msg, long len)
{
    long wr = -115;
    int spins;
    for (spins = 0; spins < 400000; ++spins) {
        wr = sys6(SYS_write, fd, (long)msg, len, 0, 0, 0);
        if (wr == len) return wr;
        if (wr != -115 && wr != -11) return wr;
    }
    return wr;
}
static long read_want(long fd, char *buf, long want)
{
    long n = 0, got = 0;
    int spins;
    for (spins = 0; spins < 400000 && got < want; ++spins) {
        n = sys6(SYS_read, fd, (long)(buf + got), want - got, 0, 0, 0);
        if (n == -115 || n == -11) continue;
        if (n <= 0) return n;
        got += n;
    }
    return got;
}
void _start(void)
{
    long fd, n, wr, rc;
    struct sockaddr_in dst;
    char buf[64];
    static const char pay3[16] = {
        'P','A','Y','3','-','A','B','C','D','E','F','G','H','I','J','K'
    };
    static const char pong16[16] = {
        'P','O','N','G','1','6','-','R','E','P','L','Y','!','!','!','!'
    };

    fd = sys6(SYS_socket, AF_INET, SOCK_STREAM, 0, 0, 0, 0);
    if (fd < 0) { ser("[f2tcp] FAIL socket\\n"); sys6(SYS_exit, 1, 0, 0, 0, 0, 0); }
    dst.sin_family = AF_INET;
    dst.sin_port = htons16(${PORT});
    dst.sin_addr = htonl32(${GUEST_DST_HEX}U);
    rc = sys6(SYS_connect, fd, (long)&dst, 16, 0, 0, 0);
    if (rc != 0 && rc != -115) {
        ser("[f2tcp] FAIL connect\\n");
        for (;;) { __asm__ volatile("pause"); }
    }
    ser("[f2tcp] CONNECT_OK\\n");

    wr = write_all(fd, "PING", 4);
    if (wr != 4) { ser("[f2tcp] FAIL write1\\n"); sys6(SYS_exit, 3, 0, 0, 0, 0, 0); }
    n = read_want(fd, buf, 4);
    if (n < 4 || !eq4(buf, "PONG")) {
        ser("[f2tcp] FAIL recv1\\n");
        sys6(SYS_exit, 4, 0, 0, 0, 0, 0);
    }
    ser("[f2tcp] RTT1_OK\\n");

    wr = write_all(fd, "PNG2", 4);
    if (wr != 4) { ser("[f2tcp] FAIL write2\\n"); sys6(SYS_exit, 5, 0, 0, 0, 0, 0); }
    n = read_want(fd, buf, 4);
    if (n < 4 || !eq4(buf, "PONG")) {
        ser("[f2tcp] FAIL recv2\\n");
        sys6(SYS_exit, 6, 0, 0, 0, 0, 0);
    }
    ser("[f2tcp] RTT2_OK\\n");

    /* Max3: larger payload on same socket (16B). */
    wr = write_all(fd, pay3, 16);
    if (wr != 16) { ser("[f2tcp] FAIL write3\\n"); sys6(SYS_exit, 7, 0, 0, 0, 0, 0); }
    n = read_want(fd, buf, 16);
    if (n < 16 || !eqn(buf, pong16, 16)) {
        ser("[f2tcp] FAIL recv3\\n");
        sys6(SYS_exit, 8, 0, 0, 0, 0, 0);
    }
    ser("[f2tcp] RTT3_OK\\n");
    ser("[f2tcp] F2_E1000_TCP_OK\\n");
    for (;;) { __asm__ volatile("pause"); }
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

# Host echo: 2×4B then 16B payload.
python3 - "$PORT" <<'PY' &
import socket, sys
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
    c.settimeout(60)
    for round_i, need, reply in (
        (1, 4, b"PONG"),
        (2, 4, b"PONG"),
        (3, 16, b"PONG16-REPLY!!!!"),
    ):
        data = b""
        while len(data) < need:
            chunk = c.recv(64)
            if not chunk:
                break
            data += chunk
        if len(data) >= need:
            c.sendall(reply)
            print("HOST_ECHO_OK", round_i, data[:need], flush=True)
        else:
            print("HOST_BAD", round_i, data, flush=True)
            break
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

echo '=== F2 e1000 TCP Max3 ==='
fail=0
for tag in CONNECT_OK RTT1_OK RTT2_OK RTT3_OK F2_E1000_TCP_OK; do
  if grep -aq "$tag" "$QLOG"; then
    echo "PASS $tag"
  else
    echo "FAIL $tag"
    fail=1
  fi
done
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[f2tcp\]|\[TCP\]|\[NET\]|F2_|PANIC|Page Fault' "$QLOG" | tail -50
exit "$fail"
