#!/usr/bin/env bash
# P11_MUSL_GUEST_QEMU — musl static ET_EXEC on booted guest via syscall path.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/kernel.elf"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "P11_MUSL_GUEST_QEMU: SKIP (no qemu-system-x86_64)"
	exit 0
fi

echo "== qemu_musl_guest_smoke: build kernel (musl boot preferred) =="
make -C "${ROOT}" -s kernel KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_MUSL_BOOT=1

if [[ ! -f "${KERNEL}" ]]; then
	echo "ERROR: missing ${KERNEL}" >&2
	exit 1
fi

out="$(timeout 5 qemu-system-x86_64 -kernel "${KERNEL}" -nographic \
	-device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"MUSL_STATIC"* ]]; then
	echo "P11_MUSL_GUEST_QEMU: PASS"
	exit 0
fi

echo "P11_MUSL_GUEST_QEMU: FAIL (output: ${out})" >&2
exit 1
