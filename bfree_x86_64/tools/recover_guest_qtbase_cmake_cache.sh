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
# shellcheck source=tools/resolve_elf_musl_paths.sh
source "$ROOT/tools/resolve_elf_musl_paths.sh"
export_elf_musl_paths "$ROOT"

PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="$(resolve_elf_out_prefix BFREE_ELF_WAYLAND_DIR x86_64-elf-wayland "$ROOT")"
export BFREE_ELF_WAYLAND_DIR="$WAYLAND_PREFIX"
ensure_x86_64_elf_toolchain_path "$ROOT"

[[ -d "$BD" ]] || { echo "[recover-cmake] ERROR: missing $BD" >&2; exit 1; }
[[ -x "$HOST_QT/bin/qt-cmake" || -r "$HOST_QT/lib/cmake/Qt6/Qt6Config.cmake" ]] || {
  echo "[recover-cmake] ERROR: host Qt missing: $HOST_QT" >&2
  exit 1
}

export BFREE_QT_SRC="$QT_SRC"
bash "$ROOT/tools/patch_qt_guest_disable_udev.sh"
bash "$ROOT/tools/patch_qt_guest_linux_fs_h.sh"
bash "$ROOT/tools/patch_qt_guest_qsharedmemory_path_max.sh"

if [[ ! -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]]; then
  echo "[recover-cmake] cross libwayland missing — building ..."
  bash "$ROOT/tools/build_x86_64_elf_wayland.sh"
fi
scanner="$(command -v wayland-scanner 2>/dev/null || true)"
[[ -n "$scanner" ]] || {
  echo "[recover-cmake] ERROR: host wayland-scanner missing (apt install libwayland-dev)" >&2
  exit 1
}
bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$scanner"

echo "[recover-cmake] removing corrupt CMakeCache.txt / build.ninja ..."
rm -f "$BD/CMakeCache.txt" "$BD/build.ninja"
find "$BD" -name CMakeFiles -type d -prune -exec rm -rf {} + 2>/dev/null || true

echo "[recover-cmake] full configure (~10–45 min) ..."
unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
guest_qtbase_wayland_configure "$BD" "$HOST_QT" "$QT_SRC" "$WAYLAND_PREFIX" \
  2>&1 | tee "$BD/configure-recover.log"

grep -E '^FEATURE_libudev:|^FEATURE_wayland:' "$BD/CMakeCache.txt" || true
if ! grep -qE '^FEATURE_wayland:BOOL=ON$|^QT_FEATURE_wayland:BOOL=ON$' "$BD/CMakeCache.txt"; then
  echo "[recover-cmake] ERROR: FEATURE_wayland not ON — cross libwayland or Wayland_DIR missing" >&2
  grep -E 'Wayland_|FEATURE_wayland|QT_FEATURE_wayland' "$BD/CMakeCache.txt" | head -15 >&2 || true
  echo "  export BFREE_ELF_WAYLAND_DIR=\$HOME/out/x86_64-elf-wayland" >&2
  echo "  bash tools/build_x86_64_elf_wayland.sh" >&2
  echo "  bash tools/ensure_guest_qtbase_wayland.sh" >&2
  exit 1
fi
grep qdevicediscovery_udev "$BD/build.ninja" 2>/dev/null && {
  echo "[recover-cmake] ERROR: udev still in build.ninja" >&2
  exit 1
} || echo "[recover-cmake] OK: no qdevicediscovery_udev in build.ninja"

echo "[recover-cmake] done — JOBS=4 bash tools/resume_guest_qtbase_wayland_build.sh"
