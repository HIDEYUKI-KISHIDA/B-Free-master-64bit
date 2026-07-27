#!/usr/bin/env bash
# P13_LTP_OPEN_GUEST_QEMU — open/read/write/close on booted guest via syscall path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/kernel.elf"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "P13_LTP_OPEN_GUEST_QEMU: SKIP (no qemu-system-x86_64)"
	exit 0
fi

echo "== qemu_ltp_open_guest_smoke: build kernel (ltp open boot preferred) =="
make -C "${ROOT}" -s kernel KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_LTP_OPEN_BOOT=1

if [[ ! -f "${KERNEL}" ]]; then
	echo "ERROR: missing ${KERNEL}" >&2
	exit 1
fi

out="$(timeout 5 qemu-system-x86_64 -kernel "${KERNEL}" -nographic \
	-device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"LTP_OPEN_GUEST_OK"* ]]; then
	echo "P13_LTP_OPEN_GUEST_QEMU: PASS"
	exit 0
fi

echo "P13_LTP_OPEN_GUEST_QEMU: FAIL (output: ${out})" >&2
exit 1
