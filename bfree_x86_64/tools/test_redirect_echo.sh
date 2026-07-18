#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-redirect-test.log"
: > "$LOG"

(
  sleep 95
  printf 'echo MARKER > /tmp/x\n'
  sleep 3
  printf 'echo hello\n'
  sleep 3
  printf 'cat /tmp/x\n'
  sleep 3
  printf 'ls /tmp\n'
  sleep 2
) | timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ROOT/bfree.iso" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== matches ==="
grep -E 'EXCEPTION|Page Fault|root@bfree|MARKER|hello' "$LOG" | tail -40 || true
