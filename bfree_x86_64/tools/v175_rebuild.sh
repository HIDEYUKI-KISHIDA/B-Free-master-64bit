#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
for f in tools/patch_qv4engine_newinternalclass_guest.sh tools/patch_qv4mm_allocate_guest.sh tools/patch_qv4engine_guest.sh; do sed -i 's/\r$//' "$f"; done
bash tools/patch_qv4engine_guest.sh
bash tools/patch_qv4engine_newinternalclass_guest.sh
bash tools/patch_qv4mm_allocate_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/memory/qv4mm.cpp.o"
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
BFREE_FAST_TIMEOUT=300 BFREE_FAST_SYMBOLIZE=1 bash tools/guest_desktop_smoke.sh 2>&1 | tee /tmp/v175-smoke.log
