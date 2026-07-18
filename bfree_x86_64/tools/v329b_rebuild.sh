#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
PREFIX="/root/out/bfree-qt6-guest-static"

cd "$ROOT"
bash tools/patch_qqmlimport_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmlimport.cpp.o"
cmake --build "$BD" --target Qml -j4
cp -a "$BD/lib/libQt6Qml.a" "$PREFIX/lib/"
echo "[v329b] libQt6Qml strings:"
strings "$PREFIX/lib/libQt6Qml.a" | grep -E 'qrc:/qt' | head -3 || echo "[v329b] WARN: qrc strings not found"
rm -f userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
export BFREE_QT_GUEST_LINKED=1 BFREE_AUTO_LOGIN=1 BFREE_MVP_GUEST_QML=1
bash build.sh
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v329b] done"
