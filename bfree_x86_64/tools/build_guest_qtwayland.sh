#!/usr/bin/env bash
# Add QtWayland to an existing guest Qt static prefix (after qtbase + qtdeclarative).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
QT_TAG="${BFREE_QT_VERSION:-6.8.0}"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI_PREFIX="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
MUSL_SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-$ROOT/out/x86_64-elf-libm/prefix}"

if [[ "${BFREE_SKIP_QTWAYLAND:-0}" == "1" ]]; then
  echo "[qtwayland-guest] SKIP (BFREE_SKIP_QTWAYLAND=1) — ISO will use bfree QPA desktop fallback"
  exit 0
fi

if [[ -f "$GUEST_QT/lib/libQt6WaylandClient.a" ]]; then
  echo "[qtwayland-guest] already installed: $GUEST_QT/lib/libQt6WaylandClient.a"
  exit 0
fi

if [[ ! -f "$GUEST_QT/lib/libQt6Core.a" ]]; then
  echo "[qtwayland-guest] guest qtbase missing under $GUEST_QT" >&2
  echo "  bash $ROOT/tools/build_bfree_qt6_guest.sh" >&2
  exit 1
fi

fetch_qtwayland() {
  echo "[qtwayland-guest] qtwayland sources missing — fetching into $QT_SRC/qtwayland"
  if [[ -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
    return 0
  fi
  if [[ -x "$QT_SRC/init-repository" ]] && [[ ! -d "$QT_SRC/qtwayland/.git" ]]; then
    (cd "$QT_SRC" && ./init-repository --module-subset=qtwayland) || true
  elif [[ -f "$QT_SRC/init-repository" ]] && [[ ! -d "$QT_SRC/qtwayland/.git" ]]; then
    (cd "$QT_SRC" && perl init-repository --module-subset=qtwayland) || true
  fi
  if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
    rm -rf "$QT_SRC/qtwayland"
    git clone --branch "v${QT_TAG}" --depth 1 https://code.qt.io/qt/qtwayland.git \
      "$QT_SRC/qtwayland" 2>/dev/null \
      || git clone --branch "$QT_TAG" --depth 1 https://code.qt.io/qt/qtwayland.git \
      "$QT_SRC/qtwayland" 2>/dev/null \
      || git clone --depth 1 https://code.qt.io/qt/qtwayland.git "$QT_SRC/qtwayland"
  fi
  if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
    echo "[qtwayland-guest] fetch failed — still no CMakeLists.txt under $QT_SRC/qtwayland" >&2
    exit 1
  fi
}

if [[ "${1:-}" == "--fetch-only" ]]; then
  fetch_qtwayland
  exit 0
fi

if [[ ! -f "$QT_SRC/qtwayland/CMakeLists.txt" ]]; then
  fetch_qtwayland
fi

if [[ ! -f "$WAYLAND_PREFIX/lib/pkgconfig/wayland-client.pc" ]]; then
  echo "[qtwayland-guest] cross libwayland missing — building ..."
  bash "$ROOT/tools/build_x86_64_elf_wayland.sh"
fi

SCANNER_BIN="$(command -v wayland-scanner 2>/dev/null || true)"
if [[ -z "$SCANNER_BIN" ]]; then
  echo "[qtwayland-guest] host wayland-scanner missing:" >&2
  echo "  sudo apt install wayland-protocols libwayland-dev" >&2
  exit 1
fi

# Guest cross-build needs host qtwaylandscanner (Qt6WaylandScannerTools).
bash "$ROOT/tools/build_host_qtwayland_tools.sh"

# Regenerate CMake package configs (scanner path + libffi) even when libwayland is cached.
bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$SCANNER_BIN"

export PKG_CONFIG_PATH="$WAYLAND_PREFIX/lib/pkgconfig:$LIBFFI_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
unset PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR

# Qt cross toolchain expects package roots here (maps to PREFIX/lib/cmake + FIND_ROOT_PATH).
# Do NOT put the same path in both CMAKE_PREFIX_PATH and CMAKE_FIND_ROOT_PATH (Qt reroot bug).
export QT_ADDITIONAL_PACKAGES_PREFIX_PATH="${WAYLAND_PREFIX}${QT_ADDITIONAL_PACKAGES_PREFIX_PATH:+:${QT_ADDITIONAL_PACKAGES_PREFIX_PATH}}"

BD="$GUEST_QT/build-qtwayland"
rm -rf "$BD"
mkdir -p "$BD"
echo "[qtwayland-guest] configure in $BD"
echo "  Wayland=$WAYLAND_PREFIX  libffi=$LIBFFI_PREFIX  scanner=$SCANNER_BIN"
echo "  QT_HOST_PATH=$HOST_QT"
echo "  QT_ADDITIONAL_PACKAGES_PREFIX_PATH=$QT_ADDITIONAL_PACKAGES_PREFIX_PATH"
"$GUEST_QT/bin/qt-cmake" "$QT_SRC/qtwayland" \
  -DCMAKE_INSTALL_PREFIX="$GUEST_QT" \
  -DQT_HOST_PATH="$HOST_QT" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$WAYLAND_PREFIX" \
  -DWaylandScanner_EXECUTABLE="$SCANNER_BIN" \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -B "$BD" 2>&1 | tee "$BD/configure.log"

if grep -qE 'Qt6WaylandScannerTools|qtwaylandscanner' "$BD/configure.log" && \
   grep -q 'Failed to find the host tool' "$BD/configure.log"; then
  echo "[qtwayland-guest] ERROR: host qtwaylandscanner missing under $HOST_QT" >&2
  echo "  bash $ROOT/tools/build_host_qtwayland_tools.sh" >&2
  exit 1
fi

if grep -q 'QtWayland is missing required dependencies' "$BD/configure.log"; then
  echo "[qtwayland-guest] ERROR: QtWayland configure skipped module (missing Wayland deps)" >&2
  echo "  libs:" >&2
  ls -la "$WAYLAND_PREFIX/lib"/libwayland-*.a 2>&1 | tail -8 >&2 || true
  echo "  pkg-config:" >&2
  PKG_CONFIG_PATH="$WAYLAND_PREFIX/lib/pkgconfig:$LIBFFI_PREFIX/lib/pkgconfig" \
    pkg-config --print-errors --exists wayland-client wayland-server wayland-cursor wayland-egl 2>&1 | tail -5 >&2 || true
  echo "  cmake configs:" >&2
  ls -la "$WAYLAND_PREFIX/lib/cmake/Wayland" "$WAYLAND_PREFIX/lib/cmake/WaylandScanner" 2>&1 >&2 || true
  grep -E 'B-Free Wayland|WaylandScanner|Wayland FOUND|Could NOT find Wayland' "$BD/configure.log" 2>&1 | tail -10 >&2 || true
  echo "  Ensure: bash $ROOT/tools/build_x86_64_elf_wayland.sh" >&2
  echo "  Then:   bash $ROOT/tools/install_wayland_cmake_configs.sh \$(command -v wayland-scanner)" >&2
  echo "  And:    sudo apt install wayland-protocols libwayland-dev meson ninja-build" >&2
  exit 1
fi

if ! cmake --build "$BD" --target help 2>/dev/null | grep -q 'WaylandClient'; then
  echo "[qtwayland-guest] ERROR: CMake did not generate WaylandClient target" >&2
  echo "  See $BD/configure.log" >&2
  exit 1
fi

cmake --build "$BD" --target WaylandClient QWaylandIntegrationPlugin --parallel "$(nproc 2>/dev/null || echo 4)"
cmake --install "$BD" --component Devel 2>/dev/null || cmake --install "$BD"

if [[ ! -f "$GUEST_QT/lib/libQt6WaylandClient.a" ]]; then
  echo "[qtwayland-guest] ERROR: libQt6WaylandClient.a not installed under $GUEST_QT/lib" >&2
  exit 1
fi
echo "[qtwayland-guest] done: $GUEST_QT/lib/libQt6WaylandClient.a"
