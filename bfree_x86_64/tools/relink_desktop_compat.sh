#!/bin/bash
# Relink desktop.elf with updated guest_link_compat.o (pthread→CLONE_THREAD wrap).
# Qt guest libs live under /root/out — run as WSL root:
#   wsl -u root bash tools/relink_desktop_compat.sh
set -eu
# Avoid pipefail: `grep < <(strings …)` can SIGPIPE (exit 141) under pipefail.
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
export HOME=/home/h_kis
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
SRC_FALLBACK="${BFREE_DESKTOP_OBJ_FALLBACK:-/home/h_kis/bfree_build/userland/desktop_qt}"
MUSL_INC="$ROOT/out/x86_64-elf-libm/prefix/include"
export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
export BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
export BFREE_ELF_CXX_INCLUDE="${BFREE_ELF_CXX_INCLUDE:-/root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0}"
export BFREE_ROOT="$ROOT"

test -f "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a"
test -f "$MUSL_INC/stdio.h"
test -f "$DESK/Makefile.guest-elf"

cd "$DESK"

echo "[1] restore empty/missing .o from $SRC_FALLBACK (except compat)"
if [ -d "$SRC_FALLBACK" ]; then
  for f in "$SRC_FALLBACK"/*.o; do
    base=$(basename "$f")
    [ "$base" = "guest_link_compat.o" ] && continue
    if [ ! -s "$DESK/$base" ]; then
      echo "  restore $base"
      cp -f "$f" "$DESK/$base"
    fi
  done
fi

echo "[2] build guest_link_compat.o (clone wrap) + QQmlThread sync stub"
x86_64-elf-g++ -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-stack-check \
  -fno-stack-clash-protection -fno-pic \
  -Wall -Wextra -mno-sse -mno-mmx -mno-3dnow -fno-exceptions -fno-rtti -Wa,--noexecstack \
  -isystem "$MUSL_INC" -D_GNU_SOURCE \
  -x c++ -c -o "$DESK/guest_link_compat.o" "$ROOT/tools/guest_link_compat.cpp"
x86_64-elf-g++ -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-stack-check \
  -fno-stack-clash-protection -fPIC \
  -Wall -Wextra -fno-exceptions -fno-rtti -Wa,--noexecstack -std=gnu++1z -O2 \
  -isystem "$BFREE_ELF_CXX_INCLUDE" -isystem "$BFREE_ELF_CXX_INCLUDE/x86_64-pc-elf" \
  -idirafter "$MUSL_INC" -D_GNU_SOURCE -DQT_NO_DEBUG -DQT_STATIC -D__linux__ \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtQml" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtCore" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtQml/6.8.0" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtQml/6.8.0/QtQml" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtCore/6.8.0" \
  -I"$BFREE_QT_GUEST_BUILD_DIR/include/QtCore/6.8.0/QtCore" \
  -c -o "$DESK/bfree_qqmlthread_sync.o" "$DESK/bfree_qqmlthread_sync.cpp"
strings "$DESK/guest_link_compat.o" | grep -Fq 'pthread_create clone'
nm "$DESK/guest_main.o" | grep -q ' T main$'
nm "$DESK/bfree_qqmlthread_sync.o" | grep -q 'isThisThread'

echo "[3] link desktop.elf"
rm -f desktop desktop.elf
# Do not let Makefile remake guest_main / qmlcache without libstdc++ isystem
# (qmake Makefile lacks BFREE_ELF_CXX_INCLUDE → fatal type_traits).
# Also pin guest_qquick_window.o when present (manual compile with isystem).
MAKE_O=(-o guest_main.o -o guest_mvp_shell_qmlcache.o -o bfree_qqmlthread_sync.o)
if [ -f guest_desktop_shell_qmlcache.o ]; then
  MAKE_O+=(-o guest_desktop_shell_qmlcache.o)
fi
if [ -f guest_desktopshell_full_qmlcache.o ]; then
  MAKE_O+=(-o guest_desktopshell_full_qmlcache.o)
fi
if [ -f guest_gate1_window_qmlcache.o ]; then
  MAKE_O+=(-o guest_gate1_window_qmlcache.o)
fi
if [ -f guest_controls_button_qmlcache.o ]; then
  MAKE_O+=(-o guest_controls_button_qmlcache.o)
fi
if [ -f guest_breeze_theme_qmlcache.o ]; then
  MAKE_O+=(-o guest_breeze_theme_qmlcache.o)
fi
if [ -f guest_clock_applet_qmlcache.o ]; then
  MAKE_O+=(-o guest_clock_applet_qmlcache.o)
fi
if [ -f guest_wabi_indicator_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_indicator_qmlcache.o)
fi
if [ -f guest_tray_icon_button_qmlcache.o ]; then
  MAKE_O+=(-o guest_tray_icon_button_qmlcache.o)
fi
if [ -f guest_wabi_dialog_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_dialog_qmlcache.o)
fi
if [ -f guest_wabi_error_overlay_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_error_overlay_qmlcache.o)
fi
if [ -f guest_wabi_notification_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_notification_qmlcache.o)
fi
if [ -f guest_wabi_notification_center_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_notification_center_qmlcache.o)
fi
if [ -f guest_wabi_screen_area_selector_qmlcache.o ]; then
  MAKE_O+=(-o guest_wabi_screen_area_selector_qmlcache.o)
fi
if [ -f guest_splash_data.o ]; then
  MAKE_O+=(-o guest_splash_data.o)
fi
if [ -f guest_mvp_qmlcache_register.o ]; then
  MAKE_O+=(-o guest_mvp_qmlcache_register.o)
fi
if [ -f qrc_guest_desktop.o ]; then
  MAKE_O+=(-o qrc_guest_desktop.o)
fi
if [ -f guest_qquick_window.o ]; then
  MAKE_O+=(-o guest_qquick_window.o)
fi
if [ -f guest_drawhelper_init.o ]; then
  MAKE_O+=(-o guest_drawhelper_init.o)
fi
if [ -f guest_qquick_dirty_stub.o ]; then
  MAKE_O+=(-o guest_qquick_dirty_stub.o)
fi
if [ -f guest_qcoreapp_arguments.o ]; then
  MAKE_O+=(-o guest_qcoreapp_arguments.o)
fi
if [ -f guest_context_factory.o ]; then
  MAKE_O+=(-o guest_context_factory.o)
fi
make -f Makefile.guest-elf "${MAKE_O[@]}" desktop
if [ -f desktop ]; then mv -f desktop desktop.elf; fi
ls -la desktop.elf
strings desktop.elf | grep -Fq 'pthread_create clone'
cp -f desktop.elf "$ROOT/iso_root/boot/desktop.elf"
echo "PASS desktop.elf relinked with pthread→clone wrap"
