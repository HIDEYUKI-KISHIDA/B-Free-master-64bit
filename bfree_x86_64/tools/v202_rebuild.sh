#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
python3 tools/fix_v202_jssymbols_guest.py
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o" \
      "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4symbol.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
sed -i 's/mmap96-v[0-9]*/mmap96-v202/' userland/desktop_qt/guest_main.cpp
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
echo "[v202] done"
