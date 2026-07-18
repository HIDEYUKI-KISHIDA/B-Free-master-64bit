#!/usr/bin/env bash
# POSIX_CRED — uid/gid and syscall registry gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p5_cred"
"${ROOT}/build/host-tests/test_p5_syscall_gate"
echo "POSIX_CRED: PASS"
