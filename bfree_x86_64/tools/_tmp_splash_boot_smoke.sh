#!/usr/bin/env bash
# Compile splash + guest_main, relink, headless smoke for splash markers.
set -euo pipefail
export PATH=/home/h_kis/x86_64-elf-toolchain/bin:/usr/bin:/bin HOME=/home/h_kis
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
[[ -f "$QT/include/QtQuickTemplates2/6.8.0/QtQuickTemplates2/private/qquickbutton_p.h" ]] || \
  CTRL_INC="-I$QT/build-qtdeclarative/include/QtQuickTemplates2 -I$QT/build-qtdeclarative/include/QtQuickTemplates2/6.8.0 -I$QT/build-qtdeclarative/include/QtQuickTemplates2/6.8.0/QtQuickTemplates2 $CTRL_INC"
LAY_INC="-I$QT/include/QtQuickLayouts -I$QT/include/QtQuickLayouts/6.8.0 -I$QT/include/QtQuickLayouts/6.8.0/QtQuickLayouts"
[[ -f "$QT/include/QtQuickLayouts/6.8.0/QtQuickLayouts/private/qquicklinearlayout_p.h" ]] || \
  LAY_INC="-I$QT/build-qtdeclarative/include/QtQuickLayouts -I$QT/build-qtdeclarative/include/QtQuickLayouts/6.8.0 -I$QT/build-qtdeclarative/include/QtQuickLayouts/6.8.0/QtQuickLayouts $LAY_INC"

cd "$DESK"
export BFREE_GUEST_LINK_CONTROLS=1 BFREE_ELF_CXX_INCLUDE="$CXXINC" BFREE_QT_GUEST_BUILD_DIR=$QT
export BFREE_QT_BUILD_DIR=/home/h_kis/out/bfree-qt6-static BFREE_ROOT=$ROOT

echo '=== compile splash + guest_main ==='
$CXX -c $CXXFLAGS -o guest_splash_data.o guest_splash_data.cpp
$CXX -c $CXXFLAGS -DBFREE_GUEST_LINK_CONTROLS $INCPATH $CTRL_INC $LAY_INC -o guest_main.o guest_main.cpp
mkdir -p /tmp/bfree-fresh-objs /home/h_kis/bfree_build/userland/desktop_qt
cp -f guest_main.o guest_splash_data.o /tmp/bfree-fresh-objs/
[[ -f qrc_guest_desktop.o ]] && cp -f qrc_guest_desktop.o /tmp/bfree-fresh-objs/ || true
cp -f /tmp/bfree-fresh-objs/*.o /home/h_kis/bfree_build/userland/desktop_qt/ || true
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/"
bash "$ROOT/tools/relink_desktop_compat.sh"
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$DESK/desktop.elf" || true
cp -f /tmp/bfree-fresh-objs/*.o "$DESK/"
bash "$ROOT/tools/relink_desktop_compat.sh"
cp -f "$DESK/desktop.elf" "$ROOT/iso_root/boot/desktop.elf"
strings "$DESK/desktop.elf" | grep -E 'splash show ok|splash frame ok' | head -3

ISO=/tmp/bfree-ds-visual-iso
rm -rf "$ISO"; mkdir -p "$ISO/boot/grub"
cp -a "$ROOT/iso_root/boot/." "$ISO/boot/"
sed -i 's/^set default=.*/set default=1/' "$ISO/boot/grub/grub.cfg"
grub-mkrescue -o /tmp/bfree-ds-visual.iso "$ISO" -- -volid BFREE >/tmp/mk-visual.log 2>&1
cp -f /tmp/bfree-ds-visual.iso "$OUT/bfree-ds-visual.iso"

: > /tmp/bfree-ds-visual.log
timeout 360 qemu-system-x86_64 -m 2048M -no-reboot -boot d -cdrom /tmp/bfree-ds-visual.iso \
  -vga std -display none -machine pc,usb=off \
  -serial file:/tmp/bfree-ds-visual.log || true
strings -n 4 /tmp/bfree-ds-visual.log > /tmp/ds-splash-str.txt
cp -f /tmp/ds-splash-str.txt "$OUT/phase5-splash-str.txt"
grep -E 'splash|Page Fault|event loop' /tmp/ds-splash-str.txt | head -30 || true
score() { grep -aqF "$1" /tmp/ds-splash-str.txt && echo 1 || echo 0; }
s=$(score 'splash show ok')
f=$(score 'splash frame ok')
loop=$(score 'QML ready, entering event loop')
pf=$(score 'Page Fault')
echo "SHOW=$s FRAME=$f LOOP=$loop PF=$pf"
if [[ "$s" == 1 && "$f" == 1 && "$loop" == 1 && "$pf" == 0 ]]; then
  echo 'RESULT=GREEN splash-boot' | tee "$OUT/splash-boot.txt"
else
  echo 'RESULT=RED splash-boot' | tee "$OUT/splash-boot.txt"
  exit 1
fi
