#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD_CORE="/root/out/bfree-qt6-guest-static/build-qtbase"
PREFIX="/root/out/bfree-qt6-guest-static"

cd "$ROOT"
sed -i 's/\r$//' tools/patch_qthread_start_minimal_guest.sh
bash tools/patch_qthread_start_minimal_guest.sh

rm -f "$BD_CORE/src/corelib/CMakeFiles/Core.dir/thread/qthread_unix.cpp.o"
cmake --build "$BD_CORE" --target Core --parallel "$(nproc)"
cp -a "$BD_CORE/lib/libQt6Core.a" "$PREFIX/lib/"

rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
export BFREE_QT_GUEST_LINKED=1 BFREE_AUTO_LOGIN=1 BFREE_MVP_GUEST_QML=1
bash build.sh
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v347] done"
