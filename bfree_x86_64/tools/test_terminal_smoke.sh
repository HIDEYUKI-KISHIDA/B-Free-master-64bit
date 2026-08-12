#!/usr/bin/env bash
# Step 2 smoke: serial ash — uname, pipe, redirect (PR #27 terminal routing baseline).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-terminal-smoke.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
FAIL=0

if [[ ! -f "$ISO" ]]; then
  echo "SKIP: no ISO at $ISO" | tee "$LOG"
  exit 0
fi

(
  sleep 90
  printf 'uname -a\n'
  sleep 2
  printf "echo 'apos_test'\n"
  sleep 2
  printf 'ls / | wc -l\n'
  sleep 2
  printf 'echo TERM_SMOKE_OK\n'
  sleep 2
) | timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'root@bfree|TERM_SMOKE_OK|apos_test|Linux|wc' "$LOG" | tail -25 || true

grep -q 'TERM_SMOKE_OK' "$LOG" || { echo "FAIL TERM_SMOKE_OK"; FAIL=1; }
grep -qE 'root@bfree' "$LOG" || { echo "FAIL root@bfree prompt"; FAIL=1; }

if [[ "$FAIL" -eq 0 ]]; then
  echo "PASS terminal_smoke"
fi
exit "$FAIL"
