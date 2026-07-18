#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH=/root/x86_64-elf-toolchain/bin:$PATH
echo "[v118] rebuild desktop.elf..."
bash tools/build_guest_desktop_elf.sh
echo "[v118] ISO (kernel unchanged, refresh desktop)..."
export BFREE_QT_GUEST_LINKED=1 BFREE_ISO_DESKTOP_SHELL=1 BFREE_MVP_GUEST_QML=1 BFREE_AUTO_LOGIN=1
bash build.sh
echo "[v118] smoke..."
export BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=360
exec bash tools/guest_desktop_smoke.sh
