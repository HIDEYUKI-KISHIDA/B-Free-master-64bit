#!/usr/bin/env bash
set -eu
pkill -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 2
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
for f in tools/patch_qv4internalclass_inplace_guest.sh tools/patch_qv4object_guest.sh tools/fix_asproto_guest.sh tools/patch_qv4engine_guest.sh; do
    sed -i 's/\r$//' "$f"
done
bash tools/patch_qv4internalclass_inplace_guest.sh
bash tools/patch_qv4engine_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4internalclass.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
cmake --build "$BD" --target Qml -j4
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=600 bash tools/guest_desktop_smoke.sh 2>&1 | tee /tmp/v171-smoke.log
