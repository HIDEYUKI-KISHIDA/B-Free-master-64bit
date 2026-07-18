#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD_CORE="/root/out/bfree-qt6-guest-static/build-qtbase"
PREFIX="/root/out/bfree-qt6-guest-static"

cd "$ROOT"
for f in tools/patch_qqmlimport_guest.sh tools/patch_qqmlimport_addpath_guest.sh \
         tools/patch_qbindingstorage_guest.sh tools/patch_setthreaddata_guest.sh \
         tools/patch_qmetatype_registry_guest.sh; do
  sed -i 's/\r$//' "$f"
done
bash tools/patch_qqmlimport_guest.sh
bash tools/patch_qqmlimport_addpath_guest.sh
bash tools/patch_qbindingstorage_guest.sh
bash tools/patch_setthreaddata_guest.sh
bash tools/patch_qmetatype_registry_guest.sh

rm -f "$BD_CORE/src/corelib/CMakeFiles/Core.dir/kernel/qobject.cpp.o"
cmake --build "$BD_CORE" --target Core -j4
cp -a "$BD_CORE/lib/libQt6Core.a" "$PREFIX/lib/"

rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
export BFREE_QT_GUEST_LINKED=1 BFREE_AUTO_LOGIN=1 BFREE_MVP_GUEST_QML=1
bash build.sh
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v339] done"
