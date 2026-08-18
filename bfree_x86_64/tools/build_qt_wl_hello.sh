#!/usr/bin/env bash
# Link real QGuiApplication as p8test.elf (qt_wl_hello.elf).
# Not desktop.elf. Not libqbfree.a. Not BFREE_BOOT_GUI_FIRST.
# Cloud / a clean clone without the guest Qt prefix skips this; the C
# client (qt_wl_client.elf) stays in the p8test slot.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STUB="$ROOT/userland/compositor_stub"
DESK="$ROOT/userland/desktop_qt"

if [[ -x "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-g++" ]]; then
  export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
fi

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
if [[ -z "$GUEST_QT" ]]; then
  for d in "$HOME/out/bfree-qt6-guest-static" /root/out/bfree-qt6-guest-static; do
    if [[ -f "$d/lib/libQt6Gui.a" && -f "$d/lib/libQt6Core.a" ]]; then
      GUEST_QT="$d"
      break
    fi
  done
fi

need() {
  if [[ ! -f "$1" ]]; then
    echo "qt_wl_hello skip: missing $1" >&2
    echo "C p8test.elf stays the Wayland client. Guest Qt prefix is only on the maintainer tree." >&2
    exit 0
  fi
}

need_exec() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "qt_wl_hello skip: missing $1" >&2
    exit 0
  fi
}

need_exec x86_64-elf-g++
need_exec x86_64-elf-gcc
if [[ -z "$GUEST_QT" ]]; then
  echo "qt_wl_hello skip: no guest Qt prefix (libQt6Gui.a)" >&2
  exit 0
fi
need "$GUEST_QT/lib/libQt6Gui.a"
need "$GUEST_QT/lib/libQt6Core.a"
need "$DESK/crt0.o"
need "$DESK/guest_link_compat.o"
need "$DESK/guest_serial.o"
need "$DESK/desktop.ld"
need "$ROOT/tools/guest_desktop_link.sh"

export BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT"
export BFREE_GUEST_CRT0="$DESK/crt0.o"
export BFREE_GUEST_COMPAT="$DESK/guest_link_compat.o"
export BFREE_GUEST_SERIAL="$DESK/guest_serial.o"
export BFREE_ROOT="$ROOT"

CXX=x86_64-elf-g++
QT_INC="$GUEST_QT/include"
QT_VER=6.8.0
if [[ -d "$QT_INC/QtCore/6.4.0" && ! -d "$QT_INC/QtCore/$QT_VER" ]]; then
  QT_VER=6.4.0
fi
for v in 6.8.0 6.7.3 6.6.3 6.5.3 6.4.2; do
  if [[ -d "$QT_INC/QtGui/$v" ]]; then
    QT_VER="$v"
    break
  fi
done

CXXFLAGS=(
  -fno-exceptions -fno-rtti -std=gnu++17 -O2 -fPIC -fno-stack-protector
  -DQT_NO_DEBUG -DQT_STATIC -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_SSL
  -DQT_STATICPLUGIN -D__linux__ -D__x86_64__ -D_REENTRANT
  -I"$STUB"
  -I"$QT_INC"
  -I"$QT_INC/QtGui"
  -I"$QT_INC/QtCore"
  -I"$QT_INC/QtGui/$QT_VER"
  -I"$QT_INC/QtGui/$QT_VER/QtGui"
  -I"$QT_INC/QtCore/$QT_VER"
  -I"$QT_INC/QtCore/$QT_VER/QtCore"
  -I"$GUEST_QT/mkspecs/linux-g++"
)
if [[ -n "${BFREE_ELF_CXX_INCLUDE:-}" ]]; then
  CXXFLAGS+=(-isystem "$BFREE_ELF_CXX_INCLUDE")
fi
_inc="$(bash "$ROOT/tools/resolve_elf_cxx_include.sh" 2>/dev/null || true)"
if [[ -n "$_inc" ]]; then
  CXXFLAGS+=(-isystem "$_inc")
  for sub in x86_64-pc-elf x86_64-elf; do
    if [[ -f "$_inc/$sub/bits/c++config.h" ]]; then
      CXXFLAGS+=(-isystem "$_inc/$sub")
    fi
  done
fi
for musl in \
  "${BFREE_ELF_LIBM_DIR:-}/prefix/include" \
  "$ROOT/out/x86_64-elf-libm/prefix/include" \
  "$HOME/out/x86_64-elf-libm/prefix/include"; do
  if [[ -f "$musl/stdint.h" ]]; then
    CXXFLAGS+=(-idirafter "$musl")
    break
  fi
done

make -C "$STUB" wl_stub_flush.o
if ! "$CXX" "${CXXFLAGS[@]}" -c -o "$STUB/qbfree_wayland.o" "$STUB/qbfree_wayland.cpp"; then
  echo "qt_wl_hello skip: QPA compile failed. C p8test stays." >&2
  exit 0
fi
if ! "$CXX" "${CXXFLAGS[@]}" -c -o "$STUB/qt_wl_hello.o" "$STUB/qt_wl_hello.cpp"; then
  echo "qt_wl_hello skip: hello compile failed. C p8test stays." >&2
  exit 0
fi

ARCHIVES=(
  "$GUEST_QT/lib/libQt6Gui.a"
  "$GUEST_QT/lib/libQt6Core.a"
)
for a in \
  "$GUEST_QT/lib/libQt6BundledHarfbuzz.a" \
  "$GUEST_QT/lib/libQt6BundledFreetype.a" \
  "$GUEST_QT/lib/libQt6BundledLibpng.a" \
  "$GUEST_QT/lib/libQt6BundledZLIB.a" \
  "$GUEST_QT/lib/libQt6BundledPcre2.a"
do
  [[ -f "$a" ]] && ARCHIVES+=("$a")
done
for o in \
  "$GUEST_QT/lib/objects-Release/Gui_resources_1/.qt/rcc/qrc_qpdf_init.cpp.o" \
  "$GUEST_QT/lib/objects-Release/Gui_resources_2/.qt/rcc/qrc_gui_shaders_init.cpp.o"
do
  [[ -f "$o" ]] && ARCHIVES+=("$o")
done

STUBS=()
for o in "$DESK/qt_futex_guest_stub.o" "$DESK/guest_platform_stub.o" "$DESK/guest_mmap.o"; do
  [[ -f "$o" ]] && STUBS+=("$o")
done

echo "[qt_wl_hello] linking with $GUEST_QT (no libqbfree.a)"
if ! bash "$ROOT/tools/guest_desktop_link.sh" \
  -o "$STUB/qt_wl_hello.elf" \
  -T "$DESK/desktop.ld" \
  "${STUBS[@]}" \
  "$STUB/qbfree_wayland.o" \
  "$STUB/qt_wl_hello.o" \
  "$STUB/wl_stub_flush.o" \
  "${ARCHIVES[@]}"; then
  echo "qt_wl_hello skip: link failed. C p8test stays." >&2
  exit 0
fi

sz="$(wc -c < "$STUB/qt_wl_hello.elf")"
echo "QT_WL_HELLO=$STUB/qt_wl_hello.elf"
echo "QT_WL_HELLO_BYTES=$sz"
if [[ "$sz" -lt 1000000 ]]; then
  echo "qt_wl_hello: ELF too small ($sz) — not a linked QGuiApplication" >&2
  rm -f "$STUB/qt_wl_hello.elf"
  exit 0
fi
