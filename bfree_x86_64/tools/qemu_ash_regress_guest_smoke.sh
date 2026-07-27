#!/usr/bin/env bash
# P16_ASH_REGRESS_GUEST_QEMU — full BusyBox ash regress on booted guest.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${ROOT}/build/kernel.elf"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "P16_ASH_REGRESS_GUEST_QEMU: SKIP (no qemu-system-x86_64)"
	exit 0
fi

echo "== qemu_ash_regress_guest_smoke: build kernel (ash regress boot preferred) =="
make -C "${ROOT}" -s kernel KERNEL_BOOT_CFLAGS=-DBFREE_PREFER_ASH_REGRESS_BOOT=1

if [[ ! -f "${KERNEL}" ]]; then
	echo "ERROR: missing ${KERNEL}" >&2
	exit 1
fi

out="$(timeout 20 qemu-system-x86_64 -kernel "${KERNEL}" -nographic \
	-device isa-debugcon,chardev=dbg -chardev stdio,id=dbg 2>/dev/null || true)"
if [[ "${out}" == *"ASH_PIPE_GUEST_OK"* &&
      "${out}" == *"ASH_SUBSHELL_GUEST_OK"* &&
      "${out}" == *"ASH_CMDSUBST_GUEST_OK"* &&
      "${out}" == *"ASH_BG_GUEST_OK"* &&
      "${out}" == *"ASH_EXTERNAL_GUEST_OK"* &&
      "${out}" == *"ASH_DATE_GUEST_OK"* &&
      "${out}" == *"ASH_ID_GUEST_OK"* &&
      "${out}" == *"ASH_LN_READLINK_GUEST_OK"* ]]; then
	echo "P16_ASH_REGRESS_GUEST_QEMU: PASS"
	exit 0
fi

echo "P16_ASH_REGRESS_GUEST_QEMU: FAIL (output: ${out})" >&2
exit 1
