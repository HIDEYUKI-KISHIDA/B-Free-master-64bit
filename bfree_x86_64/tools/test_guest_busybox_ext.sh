#!/usr/bin/env bash
# Phase 1/2 extension smoke: /bin vFS, pipelines, applets, /tmp.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-busybox-ext.log"
: > "$LOG"

ISO="${BFREE_ISO:-$ROOT/bfree.iso}"

(
  sleep 95
  printf 'ls /bin\n'
  sleep 2
  printf 'echo hello | cat\n'
  sleep 2
  printf 'echo hello | grep hello\n'
  sleep 2
  printf 'wc -l /etc/profile\n'
  sleep 2
  printf 'find / -name busybox.elf 2>/dev/null\n'
  sleep 3
  printf 'echo MARKER > /tmp/big\n'
  sleep 2
  printf 'cat /tmp/big\n'
  sleep 2
  printf 'ls /\n'
  sleep 2
) | timeout 220 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'root@bfree|hello|MARKER|cat echo|grep echo|/bin' "$LOG" | tail -25 || true
echo "=== tail ==="
tail -35 "$LOG"
