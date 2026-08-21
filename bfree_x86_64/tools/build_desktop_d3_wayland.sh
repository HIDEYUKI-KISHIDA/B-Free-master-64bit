#!/usr/bin/env bash
# Relink desktop.elf for D3 Wayland vfork client (stub compositor path).
# Requires maintainer guest Qt prefix + desktop_qt .o tree (from Program/ or bfree_build).
# Does not overwrite daily bfree.iso.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
cd "$DESK"

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export HOME="${HOME:-/home/h_kis}"

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
need Makefile.guest-elf
if [[ -z "$MUSL_INC" ]]; then
  echo "FAIL: musl headers not found (need out/x86_64-elf-libm/prefix/include)" >&2
  exit 1
fi

echo "[d3-desktop] guest Qt=$BFREE_QT_GUEST_BUILD_DIR musl=$MUSL_INC"

if [[ -d "$SRC_FALLBACK" ]]; then
  echo "[d3-desktop] restore empty .o from $SRC_FALLBACK (except compat/main)"
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

echo "[d3-desktop] recompile guest_main.o (wayland argv when /tmp/bfree-d3-wl)"
rm -f guest_main.o
make -f Makefile.guest-elf guest_main.o

echo "[d3-desktop] link desktop.elf"
rm -f desktop desktop.elf
MAKE_O=(-o guest_main.o -o guest_link_compat.o)
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

echo "[d3-desktop] sync guest_resource_holder_va.h"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
rm -f guest_link_compat.o guest_main.o
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o
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
