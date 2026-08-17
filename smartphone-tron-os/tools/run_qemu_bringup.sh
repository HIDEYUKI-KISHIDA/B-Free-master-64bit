#!/usr/bin/env bash
# Phase 2: build qemu_virt Image and require "STOS: qemu OK" on UART.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="$ROOT/kernel"
IMAGE="$KERNEL/build/qemu_virt/stos.Image"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

make -C "$KERNEL" BOARD=qemu_virt all

set +e
timeout --signal=TERM 3 qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 128M \
  -nographic \
  -kernel "$IMAGE" \
  >"$LOG" 2>&1
qemu_rc=$?
set -e

if ! grep -q "STOS: qemu OK" "$LOG"; then
  echo "FAIL: QEMU UART did not print STOS: qemu OK" >&2
  echo "----- serial -----" >&2
  cat "$LOG" >&2
  exit 1
fi

echo "Phase 2 QEMU UART OK"
grep -E "S|STOS:" "$LOG" | head
exit 0
