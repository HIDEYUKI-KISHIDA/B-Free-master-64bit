#!/usr/bin/env bash
# Step 5 smoke: PTY open + persistent shell markers on serial console.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-pty-smoke.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
FAIL=0

if [[ ! -f "$ISO" ]]; then
  echo "SKIP: no ISO at $ISO" | tee "$LOG"
  exit 0
fi

(
  sleep 90
  printf 'exec 3<>/dev/ptmx && echo PTY_OPEN_OK\n'
  sleep 2
  printf 'cd /tmp && pwd\n'
  sleep 2
  printf 'cd /tmp && pwd\n'
  sleep 2
  printf 'echo PTY_SESSION_OK\n'
  sleep 2
) | timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'PTY_OPEN_OK|PTY_SESSION_OK|root@bfree|/tmp' "$LOG" | tail -25 || true

grep -q 'PTY_OPEN_OK' "$LOG" || { echo "FAIL PTY_OPEN_OK"; FAIL=1; }
grep -q 'PTY_SESSION_OK' "$LOG" || { echo "FAIL PTY_SESSION_OK"; FAIL=1; }

if [[ "$FAIL" -eq 0 ]]; then
  echo "PASS pty_smoke"
fi
exit "$FAIL"
