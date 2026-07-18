#!/usr/bin/env bash
# Smoke: sleep 2 & must return to prompt (cooperative fork parent resume).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-job-bg.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"

(
  for _ in $(seq 1 120); do
    if grep -q 'root@bfree' "$LOG" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  sleep 1
  printf 'sleep 2 &\n'
  sleep 2
  printf 'jobs\n'
  sleep 1
  printf 'echo DONE_JOB\n'
  sleep 1
) | timeout 160 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== guest lines ==="
grep -E 'root@bfree|sleep|jobs|DONE_JOB|dev/null|Not a tty|EXCEPTION|pause' "$LOG" | tail -40 || true

fail=0
grep -q 'DONE_JOB' "$LOG" || { echo "FAIL: no DONE_JOB (shell did not resume after sleep &)"; fail=1; }
grep -q "can't open '/dev/null'" "$LOG" && { echo "FAIL: /dev/null"; fail=1; }
grep -q 'EXCEPTION' "$LOG" && { echo "FAIL: exception"; fail=1; }
exit "$fail"
