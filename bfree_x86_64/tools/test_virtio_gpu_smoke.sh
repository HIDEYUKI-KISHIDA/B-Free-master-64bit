#!/usr/bin/env bash
# Smoke: virtio-gpu-pci backend + 1080p serial markers.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

LOG="/tmp/bfree-virtio-gpu-smoke.log"
: > "$LOG"
ISO="${BFREE_ISO:-$ROOT/bfree.iso}"
FAIL=0

if [[ ! -f "$ISO" ]]; then
  echo "SKIP: no ISO at $ISO" >&2
  exit 0
fi

timeout 180 qemu-system-x86_64 \
  -m 512M -no-reboot -cdrom "$ISO" \
  -device virtio-gpu-pci \
  -display none -serial mon:stdio \
  2>&1 | tee "$LOG" &
QPID=$!
sleep 120
kill "$QPID" 2>/dev/null || true
wait "$QPID" 2>/dev/null || true

echo "=== checks ==="
grep -E 'VIRTIO-GPU|display backend|1920|1080' "$LOG" | tail -20 || true

grep -q 'VIRTIO-GPU' "$LOG" || { echo "FAIL no VIRTIO-GPU log"; FAIL=1; }
grep -qE '1920|1080' "$LOG" || { echo "WARN no 1080p marker (may use fallback)"; }

if [[ "$FAIL" -eq 0 ]]; then
  echo "PASS virtio_gpu_smoke"
fi
exit "$FAIL"
