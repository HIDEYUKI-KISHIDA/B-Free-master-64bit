#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-busybox-test.log"
: > "$LOG"

(
  sleep 95
  printf 'pwd\n'
  sleep 2
  printf 'true\n'
  sleep 2
  printf 'cat /etc/passwd\n'
  sleep 2
  printf 'ls /\n'
  sleep 3
  printf 'echo foo > /tmp/test\n'
  sleep 2
  printf 'cat /tmp/test\n'
  sleep 2
  printf 'echo bar | cat\n'
  sleep 2
  printf 'ls /tmp\n'
  sleep 2
  printf 'rm /tmp/test\n'
  sleep 2
  printf 'ls /tmp\n'
  sleep 2
) | timeout 200 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ROOT/bfree.iso" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== tail ==="
tail -40 "$LOG"
