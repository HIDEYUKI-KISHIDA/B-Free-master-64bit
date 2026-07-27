#!/usr/bin/env bash
# LTP_OPEN — open/read/write/close via P7 invoke path.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p7_kernel_main"
"${ROOT}/build/host-tests/test_p6_syscall_invoke"
echo "LTP_OPEN: PASS"
