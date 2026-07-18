#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
sed -i 's/\r$//' tools/fix_v232_typeloader_sync_guest.py
python3 tools/fix_v232_typeloader_sync_guest.py
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmltypeloader.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
sed -i 's/mmap96-v[0-9]*/mmap96-v232/' userland/desktop_qt/guest_main.cpp
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v232] done — restart QEMU: pkill -f bfree.iso; BFREE_SKIP_BUILD=1 bash run_guest_desktop.sh"
