#!/usr/bin/env bash
# POSIX_IPC — AF_UNIX socketpair and SysV shm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p5_net_unix"
"${ROOT}/build/host-tests/test_p5_ipc_shm"
echo "POSIX_IPC: PASS"
