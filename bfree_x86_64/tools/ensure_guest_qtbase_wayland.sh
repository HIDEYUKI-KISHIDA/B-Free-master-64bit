#!/usr/bin/env bash
# Ensure guest qtbase Qt6Gui was built with the 'wayland' feature (required for QtWaylandClient).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI_PREFIX="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

guest_qt_has_wayland() {
  local features="$1/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake"
  [[ -f "$features" ]] || return 1
  grep -qE 'set\(QT_FEATURE_wayland (TRUE|"ON"|1)\)' "$features"
}

if guest_qt_has_wayland "$GUEST_QT"; then
  echo "[guest-qt-wayland] OK: Qt6Gui has wayland feature ($GUEST_QT)"
  exit 0
fi

if [[ ! -f "$GUEST_QT/lib/libQt6Gui.a" ]]; then
  echo "[guest-qt-wayland] guest Qt6Gui missing under $GUEST_QT" >&2
  echo "  bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  exit 1
fi

if [[ ! -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]]; then
  echo "[guest-qt-wayland] cross libwayland missing — building ..."
  bash "$ROOT/tools/build_x86_64_elf_wayland.sh"
fi

SCANNER_BIN="$(command -v wayland-scanner 2>/dev/null || true)"
if [[ -z "$SCANNER_BIN" ]]; then
  echo "[guest-qt-wayland] host wayland-scanner missing:" >&2
  echo "  sudo apt install wayland-protocols libwayland-dev" >&2
  exit 1
fi
bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$SCANNER_BIN"

BD="$GUEST_QT/build-qtbase"
if [[ ! -f "$BD/CMakeCache.txt" ]]; then
  echo "[guest-qt-wayland] ERROR: guest qtbase build dir missing ($BD)" >&2
  echo "  Rebuild guest qtbase with cross Wayland visible at configure time:" >&2
  echo "    export QT_ADDITIONAL_PACKAGES_PREFIX_PATH=$WAYLAND_PREFIX" >&2
  echo "    bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  exit 1
fi

echo "[guest-qt-wayland] reconfiguring guest qtbase for Qt6Gui wayland feature ..."
echo "  QT_ADDITIONAL_PACKAGES_PREFIX_PATH=$WAYLAND_PREFIX"
cd "$BD"
cmake . \
  -DQT_HOST_PATH="$HOST_QT" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$WAYLAND_PREFIX"

if ! grep -qE 'QT_FEATURE_wayland:BOOL=ON|FEATURE_wayland:BOOL=ON' CMakeCache.txt 2>/dev/null; then
  echo "[guest-qt-wayland] WARNING: wayland feature may still be OFF after reconfigure" >&2
  grep -E 'wayland' CMakeCache.txt 2>/dev/null | head -10 >&2 || true
fi

echo "[guest-qt-wayland] rebuilding Qt6Gui (and dependents) ..."
cmake --build . --target Gui --parallel "$JOBS"
cmake --install . --component Devel 2>/dev/null || cmake --install .

if ! guest_qt_has_wayland "$GUEST_QT"; then
  echo "[guest-qt-wayland] ERROR: Qt6Gui still lacks wayland feature after rebuild" >&2
  echo "  Try full guest qtbase rebuild with:" >&2
  echo "    export QT_ADDITIONAL_PACKAGES_PREFIX_PATH=$WAYLAND_PREFIX" >&2
  echo "    bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  exit 1
fi

echo "[guest-qt-wayland] OK: Qt6Gui wayland feature enabled"
