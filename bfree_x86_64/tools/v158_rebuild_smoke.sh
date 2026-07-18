#!/usr/bin/env bash
set -euo pipefail
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"

bash tools/patch_qqmlimport_guest.sh
bash tools/patch_qv4engine_guest.sh

touch /root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
cmake --build "$BD" --target Qml -j4
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/

rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/qt_futex_guest_stub.o userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1

pkill -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=600 bash tools/guest_desktop_smoke.sh 2>&1 | tee /tmp/v158-smoke.log
