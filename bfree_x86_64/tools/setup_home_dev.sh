#!/usr/bin/env bash
# Verify home-PC dev environment for B-Free POSIX track (M9+).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FAIL=0

check() {
	local name="$1"
	shift
	printf "  %-24s" "${name}"
	if "$@" >/dev/null 2>&1; then
		echo "OK"
	else
		echo "MISSING"
		FAIL=1
	fi
}

echo "== setup_home_dev: tools =="
check "gcc" command -v gcc
check "make" command -v make
check "python3" command -v python3
check "git" command -v git
check "qemu-system-x86_64" command -v qemu-system-x86_64

echo "== setup_home_dev: branch =="
BRANCH="$(git -C "${ROOT}" branch --show-current 2>/dev/null || true)"
echo "  current branch: ${BRANCH:-unknown}"
if [[ "${BRANCH}" != cursor/m9-user-boot-695c ]]; then
	echo "  hint: git fetch origin && git checkout cursor/m9-user-boot-695c"
fi

echo "== setup_home_dev: build =="
make -C "${ROOT}" -s host-tests kernel

echo "== setup_home_dev: P9 smoke =="
"${ROOT}/build/host-tests/test_p9_gdt"
"${ROOT}/build/host-tests/test_p9_user_boot"

if command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "== setup_home_dev: QEMU kernel =="
	"${ROOT}/tools/qemu_kernel_smoke.sh" || true
	echo "  (M9 goal: qemu_user_boot_smoke.sh → P9_USER_BOOT_QEMU: PASS)"
fi

if [[ "${FAIL}" -ne 0 ]]; then
	echo "SETUP: INCOMPLETE (install missing tools)"
	exit 1
fi

echo "SETUP: READY for M9 on this machine"
echo "Read: docs/M9_USER_BOOT.md"
