#!/usr/bin/env bash
# POSIX_STATIC — musl-style static ET_EXEC load + host exec shim.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p4_musl_load"
"${ROOT}/build/host-tests/test_p4_musl_exec"
echo "POSIX_STATIC: PASS"
