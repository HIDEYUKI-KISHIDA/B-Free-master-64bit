#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
bash tools/patch_qv4internalclass_revert_vtable_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4internalclass.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
rm -f userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
BFREE_FORCE_GRUB_ISO=1 BFREE_FAST_TIMEOUT=300 bash tools/guest_qml_fast_iterate.sh smoke 2>&1 | tee /tmp/v173b-smoke.log
