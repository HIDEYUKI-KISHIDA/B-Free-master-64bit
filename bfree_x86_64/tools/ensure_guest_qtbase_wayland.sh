#!/usr/bin/env bash
# Ensure guest qtbase Qt6Gui was built with the 'wayland' feature (required for QtWaylandClient).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/resolve_elf_musl_paths.sh
source "$ROOT/tools/resolve_elf_musl_paths.sh"
export_elf_musl_paths "$ROOT"

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="$(resolve_elf_out_prefix BFREE_ELF_WAYLAND_DIR x86_64-elf-wayland "$ROOT")"
LIBFFI_PREFIX="$(resolve_elf_out_prefix BFREE_ELF_LIBFFI_DIR x86_64-elf-libffi "$ROOT")"
export BFREE_ELF_WAYLAND_DIR="$WAYLAND_PREFIX"
export BFREE_ELF_LIBFFI_DIR="$LIBFFI_PREFIX"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

guest_qt_has_wayland() {
  local features="$1/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake"
  [[ -f "$features" ]] || return 1
  grep -qE 'set\(QT_FEATURE_wayland (TRUE|"ON"|1)\)' "$features"
}

guest_qtbase_cache_has_wayland() {
  local cache="$1/CMakeCache.txt"
  [[ -f "$cache" ]] || return 1
  grep -qE '^FEATURE_wayland:BOOL=ON$|^QT_FEATURE_wayland:BOOL=ON$' "$cache"
}

prepare_cross_wayland_cmake() {
  if [[ ! -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]]; then
    echo "[guest-qt-wayland] cross libwayland missing — building ..."
    bash "$ROOT/tools/build_x86_64_elf_wayland.sh"
  fi
  local scanner
  scanner="$(command -v wayland-scanner 2>/dev/null || true)"
  if [[ -z "$scanner" ]]; then
    echo "[guest-qt-wayland] host wayland-scanner missing:" >&2
    echo "  sudo apt install wayland-protocols libwayland-dev" >&2
    exit 1
  fi
  bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$scanner"
}

patch_toolchain_find_root() {
  local toolchain="$1"
  [[ -f "$toolchain" ]] || return 0
  if grep -q "$WAYLAND_PREFIX" "$toolchain"; then
    return 0
  fi
  # Append cross libwayland (+ libffi) to FIND_ROOT_PATH in guest toolchain.cmake.
  sed -i "s|set(CMAKE_FIND_ROOT_PATH \"\(.*\)\")|set(CMAKE_FIND_ROOT_PATH \"\1;${WAYLAND_PREFIX};${LIBFFI_PREFIX}\")|" \
    "$toolchain"
  echo "[guest-qt-wayland] patched toolchain FIND_ROOT_PATH: $toolchain"
}

verify_wayland_cmake_package() {
  [[ -f "$WAYLAND_PREFIX/lib/cmake/Wayland/WaylandConfig.cmake" ]] || {
    echo "[guest-qt-wayland] ERROR: missing $WAYLAND_PREFIX/lib/cmake/Wayland/WaylandConfig.cmake" >&2
    return 1
  }
  [[ -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]] || {
    echo "[guest-qt-wayland] ERROR: missing $WAYLAND_PREFIX/lib/libwayland-client.a" >&2
    return 1
  }
  echo "[guest-qt-wayland] Wayland CMake package OK: $WAYLAND_PREFIX"
}

reconfigure_guest_qtbase_wayland() {
  local bd="$1"
  local toolchain="$bd/toolchain.cmake"
  local -a unset_args=()
  local line var

  while IFS= read -r line; do
    var="${line%%:*}"
    case "$var" in
      Wayland_*|FEATURE_wayland|QT_FEATURE_wayland|QT_INTERNAL_PREVIOUSLY_FOUND_PACKAGES|QT_INTERNAL_PREVIOUSLY_SEARCHED_PACKAGES)
        unset_args+=(-U "$var")
        ;;
    esac
  done < "$bd/CMakeCache.txt"

  patch_toolchain_find_root "$toolchain"

  echo "[guest-qt-wayland] reconfiguring guest qtbase (Wayland_DIR + FIND_ROOT_PATH) ..."
  echo "  Wayland_DIR=$WAYLAND_PREFIX/lib/cmake/Wayland"
  (
    cd "$bd"
    cmake . \
      "${unset_args[@]}" \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DQT_HOST_PATH="$HOST_QT" \
      -DWayland_DIR="$WAYLAND_PREFIX/lib/cmake/Wayland" \
      2>&1 | tee "$bd/reconfigure-wayland.log"
  )
}

rebuild_guest_qtbase_gui() {
  local bd="$1"
  echo "[guest-qt-wayland] rebuilding Gui + install ..."
  cmake --build "$bd" --target Gui --parallel "$JOBS"
  cmake --install "$bd"
}

if guest_qt_has_wayland "$GUEST_QT"; then
  echo "[guest-qt-wayland] OK: Qt6Gui has wayland feature ($GUEST_QT)"
  exit 0
fi

if [[ ! -f "$GUEST_QT/lib/libQt6Gui.a" ]]; then
  echo "[guest-qt-wayland] guest Qt6Gui missing under $GUEST_QT" >&2
  echo "  export BFREE_QT_GUEST_BUILD_DIR=<dir with lib/libQt6Gui.a>" >&2
  echo "  bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  exit 1
fi

if [[ ! -w "$GUEST_QT" ]]; then
  echo "[guest-qt-wayland] ERROR: guest prefix not writable: $GUEST_QT" >&2
  echo "  use e.g. export BFREE_QT_GUEST_BUILD_DIR=\$HOME/out/bfree-qt6-guest-static" >&2
  exit 1
fi

prepare_cross_wayland_cmake
verify_wayland_cmake_package

BD="$GUEST_QT/build-qtbase"
if [[ ! -f "$BD/CMakeCache.txt" ]]; then
  echo "[guest-qt-wayland] no build-qtbase — running qtbase-only rebuild with wayland ..."
  BFREE_AUTO_CONFIRM=1 bash "$ROOT/tools/rebuild_guest_qtbase_wayland_only.sh"
  guest_qt_has_wayland "$GUEST_QT" && exit 0
  echo "[guest-qt-wayland] ERROR: rebuild_guest_qtbase_wayland_only.sh finished without wayland feature" >&2
  exit 1
fi

reconfigure_guest_qtbase_wayland "$BD"

if ! guest_qtbase_cache_has_wayland "$BD"; then
  echo "[guest-qt-wayland] reconfigure did not enable FEATURE_wayland — full qtbase rebuild ..."
  BFREE_AUTO_CONFIRM=1 bash "$ROOT/tools/rebuild_guest_qtbase_wayland_only.sh"
else
  rebuild_guest_qtbase_gui "$BD"
fi

if ! guest_qt_has_wayland "$GUEST_QT"; then
  echo "[guest-qt-wayland] ERROR: Qt6Gui still lacks wayland feature" >&2
  echo "  Manual rebuild:" >&2
  echo "    export BFREE_QT_GUEST_BUILD_DIR=$GUEST_QT" >&2
  echo "    export BFREE_ELF_WAYLAND_DIR=$WAYLAND_PREFIX" >&2
  echo "    BFREE_AUTO_CONFIRM=1 bash $ROOT/tools/rebuild_guest_qtbase_wayland_only.sh" >&2
  grep -E 'wayland|Wayland' "$BD/CMakeCache.txt" 2>/dev/null | head -15 >&2 || true
  exit 1
fi

echo "[guest-qt-wayland] OK: Qt6Gui wayland feature enabled"
