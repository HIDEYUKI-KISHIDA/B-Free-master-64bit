#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
for s in patch_qv4engine_newclass_null_guest.sh patch_qv4internalclass_init_null_other_guest.sh patch_qv4engine_postrootctx_guest.sh; do
  sed -i 's/\r$//' "tools/$s"
  bash "tools/$s"
done
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4internalclass.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
python3 tools/patch_qv4savedactivation.py
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf userland/desktop_qt/guest_resource_holder_va.h || true
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf userland/desktop_qt/guest_resource_holder_va.h
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
echo "[v186] done"
