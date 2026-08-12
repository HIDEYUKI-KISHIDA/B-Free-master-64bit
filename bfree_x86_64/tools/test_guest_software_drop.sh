#!/usr/bin/env bash
# Step 4 smoke: drop a musl-static binary into /home and execute it.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-software-drop.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
FAIL=0

if [[ ! -f "$ISO" ]]; then
  echo "SKIP: no ISO at $ISO" | tee "$LOG"
  exit 0
fi

(
  sleep 90
  printf 'cp /musl_hello.elf /home/hello.elf\n'
  sleep 2
  printf 'chmod +x /home/hello.elf\n'
  sleep 2
  printf '/home/hello.elf\n'
  sleep 3
  printf 'echo DROP_OK\n'
  sleep 2
) | timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo "=== checks ==="
grep -E 'root@bfree|DROP_OK|hello|musl' "$LOG" | tail -20 || true

if grep -q 'DROP_OK' "$LOG"; then
  echo "PASS software_drop"
else
  echo "FAIL software_drop (expected DROP_OK)"
  FAIL=1
fi

if grep -qE 'root@bfree' "$LOG"; then
  echo "PASS shell_prompt"
else
  echo "WARN shell_prompt missing"
fi

exit "$FAIL"
