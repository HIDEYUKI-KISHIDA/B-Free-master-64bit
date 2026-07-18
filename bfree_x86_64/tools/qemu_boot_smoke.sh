#!/usr/bin/env bash
# QEMU boot smoke — writes BOOT_OK to debug port 0xe9 (optional gate).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/boot_smoke.elf"

if [[ ! -f "${KERNEL}" ]]; then
  echo "ERROR: missing ${KERNEL} — run make boot-smoke" >&2
  exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "P6_BOOT_SMOKE: SKIP (no qemu-system-x86_64)"
  exit 0
fi

out="$(timeout 3 qemu-system-x86_64 -kernel "${KERNEL}" -nographic -device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"BOOT_OK"* ]]; then
  echo "P6_BOOT_SMOKE: PASS"
  exit 0
fi

echo "P6_BOOT_SMOKE: FAIL (output: ${out})" >&2
exit 1
