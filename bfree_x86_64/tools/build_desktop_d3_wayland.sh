#!/usr/bin/env bash
# Relink desktop.elf for D3 Wayland vfork client (stub compositor path).
# Links qbfree_wayland.o + wl_stub_flush.o (no libqbfree.a), -DBFREE_D3_WAYLAND_QPA.
# Requires maintainer guest Qt prefix + desktop_qt .o tree (from Program/ or bfree_build).
# Does not overwrite daily bfree.iso.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
STUB="$ROOT/userland/compositor_stub"
cd "$DESK"

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export HOME="${HOME:-/home/h_kis}"
export BFREE_D3_WAYLAND_LINK=1

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
for d in "$HOME/out/bfree-qt6-guest-static" /root/out/bfree-qt6-guest-static; do
  if [[ -z "$GUEST_QT" && -f "$d/lib/libQt6Core.a" ]]; then
    GUEST_QT="$d"
  fi
done
export BFREE_QT_GUEST_BUILD_DIR="${GUEST_QT:-/root/out/bfree-qt6-guest-static}"

MUSL_INC=""
for musl in "$ROOT/out/x86_64-elf-libm/prefix/include" \
            /root/out/x86_64-elf-libm/prefix/include \
            "$HOME/out/x86_64-elf-libm/prefix/include"; do
  if [[ -f "$musl/stdio.h" ]]; then
    MUSL_INC="$musl"
    break
  fi
done

SRC_FALLBACK="${BFREE_DESKTOP_OBJ_FALLBACK:-$HOME/bfree_build/userland/desktop_qt}"
if [[ ! -d "$SRC_FALLBACK" && -d /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt ]]; then
  SRC_FALLBACK="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt"
fi

need() {
  if [[ ! -e "$1" ]]; then
    echo "FAIL: missing $1" >&2
    exit 1
  fi
}

need "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a"
need "$ROOT/tools/guest_link_compat.cpp"
need "$STUB/qbfree_wayland.cpp"
need Makefile.guest-elf
if [[ -z "$MUSL_INC" ]]; then
  echo "FAIL: musl headers not found (need out/x86_64-elf-libm/prefix/include)" >&2
  exit 1
fi

CXX=x86_64-elf-g++
QT_INC="$BFREE_QT_GUEST_BUILD_DIR/include"
QT_VER=6.8.0
for v in 6.8.0 6.7.3 6.6.3 6.5.3 6.4.2; do
  if [[ -d "$QT_INC/QtGui/$v" ]]; then
    QT_VER="$v"
    break
  fi
done

WL_CXXFLAGS=(
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
  -I"$BFREE_QT_GUEST_BUILD_DIR/mkspecs/linux-g++"
  -idirafter "$MUSL_INC"
)

_inc="$(bash "$ROOT/tools/resolve_elf_cxx_include.sh" 2>/dev/null || true)"
if [[ -n "$_inc" ]]; then
  WL_CXXFLAGS+=(-isystem "$_inc")
  for sub in x86_64-pc-elf x86_64-elf; do
    [[ -f "$_inc/$sub/bits/c++config.h" ]] && WL_CXXFLAGS+=(-isystem "$_inc/$sub")
  done
fi

echo "[d3-desktop] guest Qt=$BFREE_QT_GUEST_BUILD_DIR musl=$MUSL_INC"

if [[ -d "$SRC_FALLBACK" ]]; then
  echo "[d3-desktop] restore empty .o from $SRC_FALLBACK (except compat/main/qpa)"
  for f in "$SRC_FALLBACK"/*.o; do
    base="$(basename "$f")"
    [[ "$base" == "guest_link_compat.o" || "$base" == "guest_main.o" ]] && continue
    if [[ ! -s "$DESK/$base" ]]; then
      echo "  restore $base"
      cp -f "$f" "$DESK/$base"
    fi
  done
fi

echo "[d3-desktop] compile guest_link_compat.o (D3 /tmp/bfree-d3-wl marker)"
rm -f guest_link_compat.o
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h" 2>/dev/null || true
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o

echo "[d3-desktop] compile qbfree_wayland.o + wl_stub_flush.o"
make -C "$STUB" wl_stub_flush.o
if ! "$CXX" "${WL_CXXFLAGS[@]}" -c -o "$STUB/qbfree_wayland.o" "$STUB/qbfree_wayland.cpp"; then
  echo "FAIL: qbfree_wayland.o compile failed" >&2
  exit 1
fi

echo "[d3-desktop] recompile guest_main.o (-DBFREE_D3_WAYLAND_QPA, /tmp/bfree-d3-wl fast path)"
rm -f guest_main.o
# Makefile.guest-elf paths may point at maintainer tree; extract flags when possible.
mk_cxxflags="$(grep -m1 '^CXXFLAGS' Makefile.guest-elf | sed 's/^CXXFLAGS[[:space:]]*=[[:space:]]*//')"
mk_incpath="$(grep -m1 '^INCPATH' Makefile.guest-elf | sed 's/^INCPATH[[:space:]]*=[[:space:]]*//')"
if [[ -n "$mk_cxxflags" && -n "$mk_incpath" ]]; then
  # shellcheck disable=SC2086
  $CXX -c $mk_cxxflags -DBFREE_D3_WAYLAND_QPA $mk_incpath -o guest_main.o guest_main.cpp
else
  make -f Makefile.guest-elf guest_main.o
fi

echo "[d3-desktop] link desktop.elf (wayland QPA, no libqbfree.a)"
rm -f desktop desktop.elf
MAKE_O=(-o guest_main.o -o guest_link_compat.o -o "$STUB/qbfree_wayland.o" -o "$STUB/wl_stub_flush.o")
for o in guest_mvp_shell_qmlcache.o bfree_qqmlthread_sync.o guest_desktop_shell_qmlcache.o \
         guest_desktopshell_full_qmlcache.o guest_gate1_window_qmlcache.o \
         guest_controls_button_qmlcache.o guest_breeze_theme_qmlcache.o \
         guest_clock_applet_qmlcache.o guest_wabi_indicator_qmlcache.o \
         guest_tray_icon_button_qmlcache.o guest_wabi_dialog_qmlcache.o \
         guest_wabi_error_overlay_qmlcache.o guest_wabi_notification_qmlcache.o \
         guest_wabi_notification_center_qmlcache.o guest_wabi_screen_area_selector_qmlcache.o \
         guest_splash_data.o guest_mvp_qmlcache_register.o qrc_guest_desktop.o \
         guest_qquick_window.o guest_drawhelper_init.o guest_qquick_dirty_stub.o \
         guest_qcoreapp_arguments.o guest_context_factory.o; do
  [[ -f "$o" ]] && MAKE_O+=(-o "$o")
done
make -f Makefile.guest-elf "${MAKE_O[@]}" desktop
[[ -f desktop ]] && mv -f desktop desktop.elf
need desktop.elf

if ! strings desktop.elf | grep -qF '[desktop_qt] D3 wayland desk session'; then
  echo "FAIL: desktop.elf lacks D3 wayland desk session string (guest_main.o stale?)" >&2
  exit 1
fi
if strings desktop.elf | grep -qF 'plugin bfree only'; then
  echo "WARN: desktop.elf still has bfree-only plugin path — check -DBFREE_D3_WAYLAND_QPA" >&2
fi

echo "[d3-desktop] sync guest_resource_holder_va.h"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
rm -f guest_link_compat.o guest_main.o
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o
# shellcheck disable=SC2086
$CXX -c $mk_cxxflags -DBFREE_D3_WAYLAND_QPA $mk_incpath -o guest_main.o guest_main.cpp 2>/dev/null || \
  make -f Makefile.guest-elf guest_main.o
rm -f desktop desktop.elf
make -f Makefile.guest-elf "${MAKE_O[@]}" desktop
[[ -f desktop ]] && mv -f desktop desktop.elf
need desktop.elf
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
holder_nm="$(nm desktop.elf 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }')"
holder_hdr="$(sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' guest_resource_holder_va.h 2>/dev/null || true)"
echo "[d3-desktop] holder nm=$holder_nm hdr=$holder_hdr"
bash "$ROOT/tools/check_desktop_holder_embedded.sh" desktop.elf

strings desktop.elf | grep -F 'build=mmap96' | head -1 || true
echo "D3_DESKTOP_ELF=$DESK/desktop.elf"
echo "D3_DESKTOP_BYTES=$(wc -c < desktop.elf)"
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
echo "Full: BFREE_D3=1 BFREE_D3_FULL=1 bash tools/_d3_compositor_stub_smoke.sh"
