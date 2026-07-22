#!/usr/bin/env bash
# P9_USER_BOOT_QEMU — ring-3 payload via in-kernel syscall path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/kernel.elf"

if [[ ! -f "${KERNEL}" ]]; then
	echo "ERROR: missing ${KERNEL}" >&2
	exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "P9_USER_BOOT_QEMU: SKIP (no qemu-system-x86_64)"
	exit 0
fi

out="$(timeout 5 qemu-system-x86_64 -kernel "${KERNEL}" -nographic \
	-device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"USER_BOOT_OK"* ]]; then
	echo "P9_USER_BOOT_QEMU: PASS"
	exit 0
fi

echo "P9_USER_BOOT_QEMU: FAIL (output: ${out})" >&2
exit 1
