#!/usr/bin/env bash
# Smoke: passwd names, multi-pid ps, mounts, kill, df, free/uptime.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-busybox-foundation.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"

(
  sleep 95
  printf 'whoami\n'
  sleep 2
  printf 'ps\n'
  sleep 3
  printf 'cat /proc/mounts\n'
  sleep 2
  printf 'df\n'
  sleep 2
  printf 'kill -0 1\n'
  sleep 2
  printf 'free\n'
  sleep 2
  printf 'uptime\n'
  sleep 2
  printf 'ls /proc\n'
  sleep 2
) | timeout 200 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'root|uid=|kworker|busybox|Mem:|load average|/proc|Filesystem|applet not|EXCEPTION' "$LOG" | tail -50 || true
echo "=== tail ==="
tail -40 "$LOG"

fail=0
grep -qE '^root$' "$LOG" || { echo "FAIL: whoami"; fail=1; }
grep -q 'Mem:' "$LOG" || { echo "FAIL: free"; fail=1; }
grep -q 'load average' "$LOG" || { echo "FAIL: uptime"; fail=1; }
grep -q 'kworker\|busybox' "$LOG" || { echo "FAIL: ps"; fail=1; }
grep -q 'tmpfs' "$LOG" || { echo "FAIL: df"; fail=1; }
grep -q 'EXCEPTION' "$LOG" && { echo "FAIL: exception"; fail=1; }
grep -q 'applet not found' "$LOG" && { echo "FAIL: missing applet"; fail=1; }
exit "$fail"
