#!/usr/bin/env bash
# F2 RX: hostfwd UDP → guest bind/recvfrom (+ optional echo PONG).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"
PORT="${BFREE_F2_UDP_PORT:-18080}"

# main.c gates net_runtime_init on -DENABLE_RUNTIME_NET; force rebuild when needed.
touch kernel/sysmain/main.c
make -C kernel ENABLE_RUNTIME_NET=1 -j4 2>&1 | tail -8

cat > /tmp/bfree_f2_udp_rx.c <<EOF
/* Freestanding: bind UDP ${PORT}, wait for PING, echo PONG. */
#include <stdint.h>
#define SYS_write 1
#define SYS_socket 41
#define SYS_sendto 44
#define SYS_recvfrom 45
#define SYS_bind 49
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
static int eq4(const char *a, const char *b)
{
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}
void _start(void)
{
    long fd, n, wr;
    struct sockaddr_in local, src;
    char buf[64];
    int spins;

    fd = sys6(SYS_socket, AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
    if (fd < 0) { ser("[f2rx] FAIL socket\\n"); sys6(SYS_exit, 1, 0, 0, 0, 0, 0); }
    local.sin_family = AF_INET;
    local.sin_port = htons16(${PORT});
    local.sin_addr = 0;
    if (sys6(SYS_bind, fd, (long)&local, 16, 0, 0, 0) != 0) {
        ser("[f2rx] FAIL bind\\n");
        sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
    }
    ser("[f2rx] READY\\n");
    /* Stay bound forever until PING — short spin budgets finish before hostfwd. */
    for (spins = 0;; ++spins) {
        n = sys6(SYS_recvfrom, fd, (long)buf, 64, 0, (long)&src, 16);
        if (n >= 4 && eq4(buf, "PING")) {
            ser("[f2rx] F2_E1000_RX_OK\\n");
            wr = sys6(SYS_sendto, fd, (long)"PONG", 4, 0, (long)&src, 16);
            if (wr == 4) {
                ser("[f2rx] F2_E1000_ECHO_OK\\n");
            } else {
                ser("[f2rx] FAIL echo\\n");
            }
            for (;;) { __asm__ volatile("pause"); }
        }
        /* Yield a little so QEMU injects RX and host script can race in. */
        if ((spins & 0xff) == 0) {
            for (n = 0; n < 1000; ++n) {
                __asm__ volatile("pause");
            }
        }
    }
}
EOF

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/f2_udp_rx_smoke.elf /tmp/bfree_f2_udp_rx.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

ISO_STAGE=/tmp/bfree-f2rx-iso
ISO=/tmp/bfree-f2rx.iso
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/f2_udp_rx_smoke.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkf2rx.log 2>&1

QLOG=/tmp/bfree-f2rx.log
: > "$QLOG"
timeout 70 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
    -netdev "user,id=n0,hostfwd=udp:127.0.0.1:${PORT}-:${PORT}" \
    -device e1000,netdev=n0 \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 &
QPID=$!

ready=0
for i in $(seq 1 90); do
  if grep -aq '\[f2rx\] READY' "$QLOG"; then
    ready=1
    break
  fi
  if ! kill -0 "$QPID" 2>/dev/null; then
    break
  fi
  sleep 0.5
done

echo "=== F2 e1000 UDP RX (ready=$ready) ==="
fail=0
if [ "$ready" != 1 ]; then
  echo 'FAIL READY'
  fail=1
else
  echo 'PASS READY'
  python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))
s.settimeout(8.0)
# a few tries: guest may still be spinning into first poll window
ok_echo = False
last_err = None
for _ in range(8):
    try:
        s.sendto(b"PING", ("127.0.0.1", port))
        data, addr = s.recvfrom(64)
        if data[:4] == b"PONG":
            print("PASS host_got_PONG from", addr)
            ok_echo = True
            break
        print("FAIL unexpected reply", data)
    except Exception as e:
        last_err = e
        time.sleep(0.4)
if not ok_echo:
    print("FAIL host_echo", last_err)
    sys.exit(2)
PY
  py_rc=$?
  if [ "$py_rc" != 0 ]; then
    fail=1
  fi
fi

# let guest print RX/ECHO lines
sleep 2
kill "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true

if grep -aq 'F2_E1000_RX_OK' "$QLOG"; then
  echo 'PASS F2_E1000_RX_OK'
else
  echo 'FAIL F2_E1000_RX_OK'
  fail=1
fi
if grep -aq 'F2_E1000_ECHO_OK' "$QLOG"; then
  echo 'PASS F2_E1000_ECHO_OK'
else
  echo 'FAIL F2_E1000_ECHO_OK'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[f2rx\]|\[NET\]|e1000|F2_|PANIC|Page Fault' "$QLOG" | tail -40
exit "$fail"
