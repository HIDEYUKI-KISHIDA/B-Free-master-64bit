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
PROGRAM_DESK="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt"
PROGRAM_ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
if [[ ! -d "$SRC_FALLBACK" && -d "$PROGRAM_DESK" ]]; then
  SRC_FALLBACK="$PROGRAM_DESK"
fi

need() {
  if [[ ! -e "$1" ]]; then
    echo "FAIL: missing $1" >&2
    exit 1
  fi
}

resolve_qpa_inc() {
  local d
  for d in \
    "$ROOT/gui_server/integration_gui/bfree_qpa" \
    "$PROGRAM_ROOT/gui_server/integration_gui/bfree_qpa" \
    "$DESK/../../gui_server/integration_gui/bfree_qpa"; do
    if [[ -f "$d/bfree/bfree_guest_abi.h" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

restore_desk_missing() {
  local name src
  for name in "$@"; do
    [[ -f "$DESK/$name" ]] && continue
    for src in "$SRC_FALLBACK" "$PROGRAM_DESK" "$HOME/bfree_build/userland/desktop_qt"; do
      [[ -f "$src/$name" ]] || continue
      cp -f "$src/$name" "$DESK/$name"
      echo "  restore $name <= $src"
      break
    done
  done
}

restore_desk_tree() {
  echo "[d3-desktop] restore missing desktop_qt headers/inc from maintainer tree"
  local dir f base
  for dir in "$SRC_FALLBACK" "$PROGRAM_DESK" "$HOME/bfree_build/userland/desktop_qt"; do
    [[ -d "$dir" ]] || continue
    for f in "$dir"/*.h "$dir"/*.inc; do
      [[ -f "$f" ]] || continue
      base="$(basename "$f")"
      [[ -f "$DESK/$base" ]] && continue
      cp -f "$f" "$DESK/$base"
      echo "  restore $base <= $dir"
    done
  done
  restore_desk_missing guest_serial.h guest_desktop_bridge.h guest_mvp_qmlcache_register.h \
    guest_breeze_tokens.h guest_mvp_shell_qml.inc guest_resource_holder_va.h
  local req
  for req in guest_desktop_bridge.h guest_mvp_qmlcache_register.h guest_breeze_tokens.h guest_serial.h; do
    if [[ ! -f "$DESK/$req" ]]; then
      echo "FAIL: missing $DESK/$req" >&2
      echo "  copy Program/bfree_x86_64/userland/desktop_qt/*.h into bfree-d2c, or set BFREE_DESKTOP_OBJ_FALLBACK" >&2
      return 1
    fi
  done
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

# Makefile.guest-elf has a qmake self-regen rule (needs desktop_qt_guest.pro).
# bfree-d2c clone often lacks the .pro — never trigger regen during D3 relink.
MAKE_GUEST_ELF=(make -f Makefile.guest-elf --assume-old=Makefile.guest-elf)

makefile_guest_var() {
  grep -m1 "^$1" Makefile.guest-elf | sed "s/^$1[[:space:]]*=[[:space:]]*//"
}

compile_guest_main_d3_o() {
  local mk_defines mk_cxx mk_cxxflags mk_incpath expanded_flags qpa_inc
  restore_desk_tree
  qpa_inc="$(resolve_qpa_inc)" || {
    echo "FAIL: bfree/bfree_guest_abi.h not found (need Program gui_server/integration_gui/bfree_qpa)" >&2
    echo "  expected under $ROOT/gui_server/integration_gui/bfree_qpa/bfree/" >&2
    return 1
  }
  mk_defines="$(makefile_guest_var DEFINES)"
  mk_cxx="$(makefile_guest_var CXX)"
  mk_cxxflags="$(makefile_guest_var CXXFLAGS)"
  mk_incpath="$(makefile_guest_var INCPATH)"
  if [[ -z "$mk_defines" || -z "$mk_cxx" || -z "$mk_cxxflags" ]]; then
    echo "FAIL: Makefile.guest-elf lacks CXX/DEFINES/CXXFLAGS" >&2
    return 1
  fi
  expanded_flags="${mk_cxxflags//\$(DEFINES)/$mk_defines -DBFREE_D3_WAYLAND_QPA}"
  if [[ -n "$MUSL_INC" ]]; then
    expanded_flags+=" -idirafter $MUSL_INC"
  fi
  rm -f guest_main.o
  echo "[d3-desktop] compile guest_main.o via $mk_cxx (-DBFREE_D3_WAYLAND_QPA qpa=$qpa_inc)"
  # shellcheck disable=SC2086
  $mk_cxx -c $expanded_flags $mk_incpath -I. -I"$qpa_inc" -o guest_main.o guest_main.cpp
  if ! strings guest_main.o | grep -qF '[desktop_qt] D3 wayland desk session'; then
    echo "FAIL: guest_main.o lacks D3 wayland session string (-DBFREE_D3_WAYLAND_QPA not applied?)" >&2
    return 1
  fi
}

echo "[d3-desktop] guest Qt=$BFREE_QT_GUEST_BUILD_DIR musl=$MUSL_INC"
restore_desk_tree

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
compile_guest_main_d3_o

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
"${MAKE_GUEST_ELF[@]}" "${MAKE_O[@]}" desktop
[[ -f desktop ]] && mv -f desktop desktop.elf
need desktop.elf

if ! strings desktop.elf | grep -qF '[desktop_qt] D3 wayland desk session'; then
  echo "FAIL: desktop.elf lacks D3 wayland desk session string (guest_main.o stale?)" >&2
  exit 1
fi
if strings desktop.elf | grep -qF 'plugin bfree only'; then
  if strings desktop.elf | grep -qF 'plugin wayland only'; then
    echo "[d3-desktop] OK: D3 wayland plugin path present (bfree path also in binary for mmap fallback)"
  else
    echo "WARN: desktop.elf lacks plugin wayland only — check -DBFREE_D3_WAYLAND_QPA" >&2
  fi
fi

echo "[d3-desktop] sync guest_resource_holder_va.h"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
rm -f guest_link_compat.o guest_main.o
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o
compile_guest_main_d3_o
rm -f desktop desktop.elf
"${MAKE_GUEST_ELF[@]}" "${MAKE_O[@]}" desktop
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
