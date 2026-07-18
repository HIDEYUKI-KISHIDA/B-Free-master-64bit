#!/usr/bin/env bash
# B3 — prepare guest Qt6 static build (mkspec + instructions). Long compile is manual/next step.
#
# Prereqs:
#   sudo apt install build-essential perl python3 git cmake ninja-build \
#     x86_64-elf-gcc x86_64-elf-binutils \
#     libfontconfig1-dev libfreetype6-dev libx11-dev libxext-dev libxrender-dev \
#     libssl-dev libglib2.0-dev bison flex gperf
#
#   bash tools/clone_qt6_src.sh          # once: download Qt sources
#   export BFREE_QT_BUILD_DIR=$HOME/out/bfree-qt6-static
#   bash tools/build_bfree_qt6_guest.sh

set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
# Guest ISO desktop.elf — separate prefix from host smoke (BFREE_QT_BUILD_DIR).
QT_BUILD="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
QT_VER="${BFREE_QT_VERSION:-6.8.0}"
MKSPEC_DIR="$ROOT/tools/qt-mkspecs/bfree-g++"

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "[B3] missing: $1" >&2
    exit 1
  }
}

need x86_64-elf-gcc
need x86_64-elf-ld
need perl
need python3

mkdir -p "$(dirname "$QT_SRC")" "$QT_BUILD" "$MKSPEC_DIR"

qt_src_ok() {
  [[ -f "$QT_SRC/qtbase/CMakeLists.txt" ]] || [[ -f "$QT_SRC/CMakeLists.txt" ]] || [[ -f "$QT_SRC/configure" ]]
}

if ! qt_src_ok; then
  echo "[B3] Qt source not found at: $QT_SRC"
  echo ""
  echo "Run (recommended on native WSL disk, not /mnt/c):"
  echo "  export BFREE_QT_SRC=\$HOME/src/qt6"
  echo "  bash $ROOT/tools/clone_qt6_src.sh"
  echo ""
  echo "Or point BFREE_QT_SRC at an existing Qt $QT_VER tree with qtbase + qtdeclarative."
  exit 1
fi

mkdir -p "$MKSPEC_DIR"
cat >"$MKSPEC_DIR/qmake.conf" <<'EOF'
include(../../linux-g++/qmake.conf)
QMAKE_CC = x86_64-elf-gcc
QMAKE_CXX = x86_64-elf-g++
QMAKE_LINK = x86_64-elf-g++
QMAKE_AR = x86_64-elf-ar cqs
QMAKE_OBJCOPY = x86_64-elf-objcopy
QMAKE_STRIP = x86_64-elf-strip
EOF

echo "[B3-guest] Qt sources: $QT_SRC"
echo "[B3-guest] Guest install prefix: $QT_BUILD"
echo "[B3-guest] Host smoke prefix (already done?): $HOST_QT"
echo "[B3-guest] mkspec: $MKSPEC_DIR"
echo ""
echo "=== Step 1 — Host Qt (WSL smoke, DONE if you built /root/out/bfree-qt6-static) ==="
echo "  export BFREE_QT_BUILD_DIR=$HOST_QT"
echo "  # configure qtbase + qtdeclarative like you already did"
echo ""
echo "=== Step 2 — Guest Qt for ISO desktop.elf (hours; use native Linux fs under /root) ==="
echo ""
if [[ -d "$QT_SRC/qtbase" ]]; then
  echo "  export BFREE_QT_GUEST_BUILD_DIR=$QT_BUILD"
  echo "  export BFREE_GUEST_CC=x86_64-elf-gcc"
  echo "  export BFREE_GUEST_CXX=x86_64-elf-g++"
  echo "  mkdir -p \"$QT_BUILD/build-qtbase\" && cd \"$QT_BUILD/build-qtbase\""
  echo "  \"$QT_SRC/qtbase/configure\" -prefix \"$QT_BUILD\" \\"
  echo "    -xplatform linux-g++ -device-option CROSS_COMPILE=x86_64-elf- \\"
  echo "    -static -release -opensource -confirm-license \\"
  echo "    -no-opengl -no-vulkan -no-dbus \\"
  echo "    -qt-zlib -qt-libpng -qt-freetype -qt-harfbuzz \\"
  echo "    -skip qtwebengine -nomake examples -nomake tests"
  echo "  cmake --build . --parallel \"\$(nproc)\""
  echo "  cmake --install ."
  echo "  # qtdeclarative: qt-cmake in separate build dir (same as host), prefix $QT_BUILD"
  echo ""
  echo "  Experimental (2-A): -xplatform $MKSPEC_DIR after qtbase patches for freestanding."
else
  echo "  cd \"$QT_SRC\""
  echo "  ./configure -prefix \"$QT_BUILD\" -static -release ... (see GUEST_QT_DESKTOP.txt)"
fi
echo ""
echo "=== Step 3 — guest desktop.elf + ISO (after libQt6Core.a exists) ==="
echo "  bash $ROOT/tools/check_b5_prereqs.sh"
echo "  bash $ROOT/tools/build_guest_desktop_elf.sh"
echo "  export BFREE_QT_GUEST_LINKED=1"
echo "  bash $ROOT/build_mvp_native_desktop.sh run"
echo ""
echo "B5a QML on ISO is already staged by stage_mvp_qml.sh (DesktopShell.qml tree)."
echo "B5b loads GuestMvpShell.qml from qrc in guest desktop.elf first; then file:///bfree/qml/..."
echo ""
if [[ -f "$QT_BUILD/lib/libQt6Core.a" ]]; then
  echo "[B3-guest] READY: $QT_BUILD/lib/libQt6Core.a"
  echo "  Run: bash $ROOT/tools/build_guest_desktop_elf.sh"
else
  echo "[B3-guest] Not ready yet — MVP keeps desktop_stub on ISO until guest Qt is built."
fi
