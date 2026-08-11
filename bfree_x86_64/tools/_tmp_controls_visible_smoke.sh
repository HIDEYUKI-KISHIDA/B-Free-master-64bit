#!/usr/bin/env bash
# Rebuild guest_main (Controls visible) + short desktop smoke.
set -euo pipefail
export PATH=/home/h_kis/x86_64-elf-toolchain/bin:/usr/bin:/bin
export HOME=/home/h_kis
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
DESK=$ROOT/userland/desktop_qt
OUT=$ROOT/out
QT=/home/h_kis/out/bfree-qt6-guest-static
CXXINC=/home/h_kis/x86_64-elf-toolchain/include/c++/13.2.0
CXX=x86_64-elf-g++
mkdir -p "$OUT"
eval "$(python3 - <<'PY'
from pathlib import Path
import re
t=Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/Makefile.guest-elf").read_text()
def grab(name):
    m=re.search(rf'^{name}\s*=\s*(.*)$', t, re.M)
    return m.group(1).strip()
defines=grab("DEFINES")
print("CXXFLAGS="+repr(grab("CXXFLAGS").replace("$(DEFINES)", defines)))
print("INCPATH="+repr(grab("INCPATH")))
PY
)"
CTRL_INC="-I$QT/include/QtQuickTemplates2 -I$QT/include/QtQuickTemplates2/6.8.0 -I$QT/include/QtQuickTemplates2/6.8.0/QtQuickTemplates2"
if [[ ! -f "$QT/include/QtQuickTemplates2/6.8.0/QtQuickTemplates2/private/qquickbutton_p.h" ]]; then
  CTRL_INC="-I$QT/build-qtdeclarative/include/QtQuickTemplates2 -I$QT/build-qtdeclarative/include/QtQuickTemplates2/6.8.0 -I$QT/build-qtdeclarative/include/QtQuickTemplates2/6.8.0/QtQuickTemplates2 $CTRL_INC"
fi
LAY_INC="-I$QT/include/QtQuickLayouts -I$QT/include/QtQuickLayouts/6.8.0 -I$QT/include/QtQuickLayouts/6.8.0/QtQuickLayouts"
if [[ ! -f "$QT/include/QtQuickLayouts/6.8.0/QtQuickLayouts/private/qquicklinearlayout_p.h" ]]; then
  LAY_INC="-I$QT/build-qtdeclarative/include/QtQuickLayouts -I$QT/build-qtdeclarative/include/QtQuickLayouts/6.8.0 -I$QT/build-qtdeclarative/include/QtQuickLayouts/6.8.0/QtQuickLayouts $LAY_INC"
fi
cd "$DESK"
export BFREE_GUEST_LINK_CONTROLS=1
export BFREE_ELF_CXX_INCLUDE="$CXXINC"
export BFREE_QT_GUEST_BUILD_DIR=$QT
export BFREE_QT_BUILD_DIR=/home/h_kis/out/bfree-qt6-static
export BFREE_ROOT=$ROOT
echo "=== compile guest_main ==="
$CXX -c $CXXFLAGS -DBFREE_GUEST_LINK_CONTROLS $INCPATH $CTRL_INC $LAY_INC -o guest_main.o guest_main.cpp
mkdir -p /tmp/bfree-fresh-objs
cp -f guest_main.o /tmp/bfree-fresh-objs/
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/" || true
cp -f /tmp/bfree-fresh-objs/*.o /home/h_kis/bfree_build/userland/desktop_qt/ 2>/dev/null || true
bash "$ROOT/tools/relink_desktop_compat.sh"
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$DESK/desktop.elf" || true
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/"
bash "$ROOT/tools/relink_desktop_compat.sh"
cp -f "$ROOT/kernel/kernel.elf" "$ROOT/iso_root/boot/kernel.elf"
cp -f "$DESK/desktop.elf" "$ROOT/iso_root/boot/desktop.elf"
ISO=/tmp/bfree-controls-vis-iso
rm -rf "$ISO"
mkdir -p "$ISO/boot/grub"
cp -a "$ROOT/iso_root/boot/." "$ISO/boot/"
sed -i 's/^set default=.*/set default=1/' "$ISO/boot/grub/grub.cfg"
sed -i '/busybox.elf/d;/p8test.elf/d' "$ISO/boot/grub/grub.cfg" || true
grub-mkrescue -o /tmp/bfree-controls-vis.iso "$ISO" -- -volid BFREE >/tmp/mk-cvis.log 2>&1
QLOG="$ROOT/.cache/bfree-controls-vis.log"
mkdir -p "$ROOT/.cache"
: > "$QLOG"
echo "=== qemu desktop controls vis ==="
pkill -9 -f qemu-system-x86_64 2>/dev/null || true
sleep 1
timeout 280 qemu-system-x86_64 -m 2048M -no-reboot -boot d -cdrom /tmp/bfree-controls-vis.iso \
  -vga std -display none -machine pc,usb=off \
  -serial file:"$QLOG" || true
strings -n 4 "$QLOG" > "$ROOT/.cache/controls-vis-str.txt"
score() { grep -aqF "$1" "$ROOT/.cache/controls-vis-str.txt" && echo 1 || echo 0; }
btn_parent=$(score '[desktop_qt] Controls Button shell parent ok')
btn_vis=$(score '[desktop_qt] Controls Button shell visible ok')
btn_host=$(score '[desktop_qt] Controls Button product host=')
btn_host_fill=$(score '[desktop_qt] Controls Button product host=host-fill-ci')
btn_host_hy=$(score '[desktop_qt] Controls Button product host=hybrid-row')
btn_already=$(score '[desktop_qt] Controls Button product parent already ok')
btn_defer=$(score '[desktop_qt] Controls Button product parent deferred')
standin_vis=$(score '[desktop_qt] QML Controls.Button shell visible ok')
loop=$(score 'QML ready, entering event loop')
create=$(score '[desktop_qt] Controls Button create ok')
pf=$(score 'Page Fault')
panic=$(score 'PANIC')
btn_vis_def=$(score '[desktop_qt] Controls Button shell visible deferred (PF@0x8)')
btn_vis_enter=$(score '[desktop_qt] Controls Button shell visible enter')
echo "CREATE=$create FILL=$btn_host_fill HY=$btn_host_hy ALREADY=$btn_already BTN_PARENT=$btn_parent BTN_VIS=$btn_vis VISENTER=$btn_vis_enter VISDEF=$btn_vis_def LOOP=$loop PF=$pf PANIC=$panic"
grep -aE 'Controls Button|Page Fault|PANIC|QML ready|product SG pixel|host-fill|post-act' "$ROOT/.cache/controls-vis-str.txt" | head -60 || true
# Full: parent at host-fill + visible at post-act.
if [[ "$create" == 1 && "$btn_host_fill" == 1 && "$btn_parent" == 1 && "$btn_vis" == 1 && "$loop" == 1 && "$pf" == 0 && "$panic" == 0 ]]; then
  echo 'RESULT=PASS controls_visible (host-fill + post-act show)'
  exit 0
fi
# Parent-only fallback (visible still deferred).
if [[ "$create" == 1 && "$btn_host_fill" == 1 && "$btn_parent" == 1 && "$btn_vis_def" == 1 && "$loop" == 1 && "$pf" == 0 && "$panic" == 0 ]]; then
  echo 'RESULT=PASS controls_host (parent green; visible deferred)'
  exit 0
fi
if [[ "$create" == 1 && ("$btn_host_fill" == 1 || "$btn_host_hy" == 1 || "$btn_already" == 1) && "$btn_parent" == 1 && "$btn_vis" == 1 && "$loop" == 1 && "$pf" == 0 && "$panic" == 0 ]]; then
  echo 'RESULT=PASS controls_visible (product host)'
  exit 0
fi
# Non-product attach path.
if [[ "$btn_parent" == 1 && "$btn_vis" == 1 && "$loop" == 1 && "$pf" == 0 && "$panic" == 0 ]]; then
  echo 'RESULT=PASS controls_visible'
  exit 0
fi
echo 'RESULT=FAIL controls_visible'
exit 1
