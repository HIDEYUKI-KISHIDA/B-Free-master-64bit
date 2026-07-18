#!/usr/bin/env bash
# Test /proc filesystem implementation in B-Free
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-proc-test.log"
: > "$LOG"

(
  sleep 95
  printf 'cat /proc/version\n'
  sleep 3
  printf 'cat /proc/cpuinfo\n'
  sleep 3
  printf 'cat /proc/meminfo\n'
  sleep 3
  printf 'cat /proc/uptime\n'
  sleep 3
  printf 'cat /proc/1/stat\n'
  sleep 3
  printf 'cat /proc/1/status\n'
  sleep 3
  printf 'cat /proc/self/cmdline\n'
  sleep 3
  printf 'ls /proc\n'
  sleep 3
  printf 'ls /proc/1\n'
  sleep 3
  printf 'ps\n'
  sleep 5
) | timeout 200 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ROOT/bfree.iso" -display none -serial mon:stdio \
  2>&1 | tee "$LOG"

echo ""
echo "=== PROC TEST RESULTS ==="
echo "--- /proc/version ---"
grep -A2 'cat /proc/version' "$LOG" | tail -2 || echo "(not found)"
echo "--- /proc/cpuinfo ---"
grep -A5 'cat /proc/cpuinfo' "$LOG" | tail -5 || echo "(not found)"
echo "--- /proc/meminfo ---"
grep -A3 'cat /proc/meminfo' "$LOG" | tail -3 || echo "(not found)"
echo "--- /proc/1/stat ---"
grep -A2 'cat /proc/1/stat' "$LOG" | tail -2 || echo "(not found)"
echo "--- ps ---"
grep -A5 'ps' "$LOG" | tail -5 || echo "(not found)"
echo "========================="
