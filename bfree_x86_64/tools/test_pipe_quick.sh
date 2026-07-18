#!/usr/bin/env bash
set -eu
pkill -9 -f 'qemu-system-x86_64.*bfree' 2>/dev/null || true
sleep 1
LOG=/tmp/bfree-pipe.log
: > "$LOG"
ISO="${BFREE_ISO:-/tmp/bfree-test.iso}"
(
  sleep 30
  printf 'echo hello | cat\n'
  sleep 5
  printf 'echo done\n'
  sleep 2
) | timeout 90 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
  -display none -serial mon:stdio 2>&1 | tee "$LOG"
grep -E 'hello|done|root@bfree' "$LOG" | tail -10 || true
