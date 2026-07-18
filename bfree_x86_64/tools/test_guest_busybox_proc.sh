#!/usr/bin/env bash
# Smoke: minimal /proc + ps / free / uptime in guest.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-busybox-proc.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"

(
  sleep 95
  printf 'free\n'
  sleep 2
  printf 'uptime\n'
  sleep 2
  printf 'ls /proc\n'
  sleep 2
  printf 'ps\n'
  sleep 3
) | timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'Mem:|load average|\(busybox\)|PID|applet not|can.t open' "$LOG" | tail -40 || true
echo "=== tail ==="
tail -35 "$LOG"

fail=0
grep -q 'Mem:' "$LOG" || { echo "FAIL: free/Mem missing"; fail=1; }
grep -q 'load average' "$LOG" || { echo "FAIL: uptime missing"; fail=1; }
grep -Eq 'busybox|sh' "$LOG" || { echo "FAIL: ps output missing"; fail=1; }
grep -q "can't open '/proc'" "$LOG" && { echo "FAIL: /proc still broken"; fail=1; }
grep -q 'applet not found' "$LOG" && { echo "FAIL: applet missing"; fail=1; }
exit "$fail"
