#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
python3 tools/fix_v258b_typedata_extern.py
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmltypedata.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmlengine.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
objdump -d userland/desktop_qt/desktop.elf | grep -A5 'qml_register_types_QML' | head -8
echo "[v258] link done"
