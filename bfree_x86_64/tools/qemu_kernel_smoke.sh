#!/usr/bin/env bash
# P7_KERNEL_BOOT — QEMU smoke for freestanding kernel entry.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/kernel.elf"

if [[ ! -f "${KERNEL}" ]]; then
  echo "ERROR: missing ${KERNEL}" >&2
  exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "P7_KERNEL_BOOT: SKIP (no qemu-system-x86_64)"
  exit 0
fi

out="$(timeout 3 qemu-system-x86_64 -kernel "${KERNEL}" -nographic \
  -device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"KERNEL_OK"* ]]; then
  echo "P7_KERNEL_BOOT: PASS"
  exit 0
fi

echo "P7_KERNEL_BOOT: FAIL (output: ${out})" >&2
exit 1
