#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"

cd "$ROOT"
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
export BFREE_QT_GUEST_LINKED=1 BFREE_AUTO_LOGIN=1 BFREE_MVP_GUEST_QML=1
bash build.sh
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v348] done"
