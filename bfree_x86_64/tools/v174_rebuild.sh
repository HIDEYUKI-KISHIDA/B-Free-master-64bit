#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
bash tools/patch_qv4engine_classobject_heap_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
BFREE_FORCE_GRUB_ISO=1 bash tools/fast_update_iso_desktop.sh 2>/dev/null || BFREE_FORCE_GRUB_ISO=1 bash -c 'grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1'
BFREE_FAST_TIMEOUT=300 BFREE_FAST_SYMBOLIZE=1 bash tools/guest_qml_fast_iterate.sh smoke 2>&1 | tee /tmp/v174-smoke.log
