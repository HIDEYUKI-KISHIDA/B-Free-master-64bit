#!/usr/bin/env bash
# Recover corrupt CMakeCache: drop cache+ninja, full configure (keeps toolchain.cmake).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/guest_qtbase_wayland_configure.sh
source "$ROOT/tools/guest_qtbase_wayland_configure.sh"

PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"

[[ -d "$BD" ]] || { echo "[recover-cmake] ERROR: missing $BD" >&2; exit 1; }
[[ -x "$HOST_QT/bin/qt-cmake" || -r "$HOST_QT/lib/cmake/Qt6/Qt6Config.cmake" ]] || {
  echo "[recover-cmake] ERROR: host Qt missing: $HOST_QT" >&2
  exit 1
}

export BFREE_QT_SRC="$QT_SRC"
bash "$ROOT/tools/patch_qt_guest_disable_udev.sh"
bash "$ROOT/tools/patch_qt_guest_linux_fs_h.sh"
bash "$ROOT/tools/patch_qt_guest_qsharedmemory_path_max.sh"

echo "[recover-cmake] removing corrupt CMakeCache.txt / build.ninja ..."
rm -f "$BD/CMakeCache.txt" "$BD/build.ninja"
find "$BD" -name CMakeFiles -type d -prune -exec rm -rf {} + 2>/dev/null || true

echo "[recover-cmake] full configure (~10–45 min) ..."
unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
guest_qtbase_wayland_configure "$BD" "$HOST_QT" "$QT_SRC" "$WAYLAND_PREFIX" \
  2>&1 | tee "$BD/configure-recover.log"

grep -E '^FEATURE_libudev:|^FEATURE_wayland:' "$BD/CMakeCache.txt" || true
grep qdevicediscovery_udev "$BD/build.ninja" 2>/dev/null && {
  echo "[recover-cmake] ERROR: udev still in build.ninja" >&2
  exit 1
} || echo "[recover-cmake] OK: no qdevicediscovery_udev in build.ninja"

echo "[recover-cmake] done — JOBS=4 bash tools/resume_guest_qtbase_wayland_build.sh"
