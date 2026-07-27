#!/usr/bin/env bash
# LTP-style user boot gate (M9).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/host-tests"

"${BUILD}/test_p9_gdt"
"${BUILD}/test_p9_user_boot"
"${ROOT}/tools/qemu_user_boot_smoke.sh"
echo "LTP_USER_BOOT: PASS"
