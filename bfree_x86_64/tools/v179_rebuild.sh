#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
cd "$ROOT"
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf userland/desktop_qt/guest_resource_holder_va.h
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf userland/desktop_qt/guest_resource_holder_va.h
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
echo "[v179] holder=$(grep BFREE_GUEST userland/desktop_qt/guest_resource_holder_va.h) done"
