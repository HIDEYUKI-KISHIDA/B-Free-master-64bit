#!/usr/bin/env bash
# LTP-style paging + initramfs gate (M8).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${ROOT}/build/host-tests"

"${BUILD}/test_p8_paging"
"${BUILD}/test_p8_initramfs"
echo "LTP_PAGING_INITRAMFS: PASS"
