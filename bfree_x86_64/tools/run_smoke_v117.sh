#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH=/root/x86_64-elf-toolchain/bin:$PATH
export BFREE_SMOKE_REBUILD=1
export BFREE_SMOKE_TIMEOUT=360
exec bash tools/guest_desktop_smoke.sh
