#!/usr/bin/env bash
# Rebuild guest qtbase only, with cross libwayland visible so Qt6Gui gets FEATURE_wayland.
# Hours (qtbase only). Non-interactive when BFREE_AUTO_CONFIRM=1.
#
#   export BFREE_QT_GUEST_BUILD_DIR=$HOME/out/bfree-qt6-guest-static
#   export BFREE_ELF_WAYLAND_DIR=$HOME/.../out/x86_64-elf-wayland
#   BFREE_AUTO_CONFIRM=1 bash tools/rebuild_guest_qtbase_wayland_only.sh
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI_PREFIX="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"

guest_qt_has_wayland() {
  local features="$1/lib/cmake/Qt6Gui/Qt6GuiFeatures.cmake"
  [[ -f "$features" ]] && grep -qE 'set\(QT_FEATURE_wayland (TRUE|"ON"|1)\)' "$features"
}

if guest_qt_has_wayland "$PREFIX"; then
  echo "[guest-qtbase-wayland] OK: Qt6Gui already has wayland ($PREFIX)"
  exit 0
fi

echo "=== Rebuild guest qtbase with Wayland (Gui feature only) ==="
echo "  prefix:  $PREFIX"
echo "  wayland: $WAYLAND_PREFIX"
echo "  host:    $HOST_QT"
echo "  jobs:    $JOBS"

if [[ -z "${BFREE_AUTO_CONFIRM:-}" ]]; then
  read -r -p "Continue? [y/N] " ans
  case "$ans" in y|Y|yes|YES) ;; *) echo "Aborted."; exit 0 ;; esac
fi

# Reuse env/toolchain setup from rebuild_guest_qt_minimal (same prereqs).
BFREE_QT_WAYLAND=1 \
BFREE_AUTO_CONFIRM=1 \
BFREE_QT_SRC="$QT_SRC" \
BFREE_QT_GUEST_BUILD_DIR="$PREFIX" \
BFREE_QT_BUILD_DIR="$HOST_QT" \
BFREE_ELF_WAYLAND_DIR="$WAYLAND_PREFIX" \
BFREE_ELF_LIBFFI_DIR="$LIBFFI_PREFIX" \
JOBS="$JOBS" \
bash "$ROOT/tools/rebuild_guest_qtbase_wayland_impl.sh"

if ! guest_qt_has_wayland "$PREFIX"; then
  echo "[guest-qtbase-wayland] ERROR: FEATURE_wayland still OFF after rebuild" >&2
  grep -E 'wayland|Wayland' "$PREFIX/build-qtbase/CMakeCache.txt" 2>/dev/null | head -20 >&2 || true
  exit 1
fi
echo "[guest-qtbase-wayland] OK: Qt6Gui wayland feature enabled"
