#!/usr/bin/env bash
# Minimal desktop relink after guest_link_compat PHDR/TLS fix (WSL vfork __copy_tls GP).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
cd "$DESK"

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export HOME="${HOME:-/home/h_kis}"

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
for d in "$HOME/out/bfree-qt6-guest-static" /root/out/bfree-qt6-guest-static; do
  [[ -z "$GUEST_QT" && -f "$d/lib/libQt6Core.a" ]] && GUEST_QT="$d"
done
export BFREE_QT_GUEST_BUILD_DIR="${GUEST_QT:-/root/out/bfree-qt6-guest-static}"

MUSL_INC=""
for musl in "$ROOT/out/x86_64-elf-libm/prefix/include" \
            /root/out/x86_64-elf-libm/prefix/include \
            "$HOME/out/x86_64-elf-libm/prefix/include"; do
  [[ -f "$musl/stdio.h" ]] && MUSL_INC="$musl" && break
done
[[ -n "$MUSL_INC" ]] || { echo "FAIL: musl headers" >&2; exit 1; }
[[ -f Makefile.guest-elf ]] || { echo "FAIL: $DESK/Makefile.guest-elf" >&2; exit 1; }
[[ -s desktop.elf ]] || { echo "FAIL: need existing desktop.elf to emit phdrs" >&2; exit 1; }

echo "[phdrs-relink] sync bfree_guest_phdrs from desktop.elf"
python3 "$ROOT/tools/emit_guest_compat_phdrs.py" "$DESK/desktop.elf" "$ROOT/tools/guest_link_compat.cpp"

echo "[phdrs-relink] compile guest_link_compat.o"
rm -f guest_link_compat.o
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o

echo "[phdrs-relink] link desktop.elf (compat only — keep existing .o set)"
MAKE_O=(-o guest_link_compat.o -o guest_main.o)
for o in guest_mvp_shell_qmlcache.o bfree_qqmlthread_sync.o guest_desktop_shell_qmlcache.o \
         guest_desktopshell_full_qmlcache.o guest_gate1_window_qmlcache.o \
         guest_controls_button_qmlcache.o guest_breeze_theme_qmlcache.o \
         guest_clock_applet_qmlcache.o guest_tray_icon_button_qmlcache.o \
         guest_wabi_dialog_qmlcache.o guest_wabi_error_overlay_qmlcache.o \
         guest_wabi_notification_qmlcache.o guest_wabi_notification_center_qmlcache.o \
         guest_wabi_screen_area_selector_qmlcache.o guest_splash_data.o \
         guest_mvp_qmlcache_register.o qrc_guest_desktop.o guest_qquick_window.o \
         guest_drawhelper_init.o guest_qquick_dirty_stub.o guest_qcoreapp_arguments.o \
         guest_context_factory.o; do
  [[ -f "$o" ]] && MAKE_O+=(-o "$o")
done
rm -f desktop desktop.elf
make -f Makefile.guest-elf "${MAKE_O[@]}" desktop
[[ -f desktop ]] && mv -f desktop desktop.elf

bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" desktop.elf
bash "$ROOT/tools/check_desktop_holder_embedded.sh" desktop.elf
sha256sum desktop.elf
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
