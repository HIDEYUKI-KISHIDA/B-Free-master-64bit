#!/usr/bin/env bash
# POSIX_FS — open/read/write device nodes and mount.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
"${ROOT}/build/host-tests/test_p5_devnode"
"${ROOT}/build/host-tests/test_p5_mount"
echo "POSIX_FS: PASS"
