#!/usr/bin/env bash
set -eu
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
cd "$ROOT"
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf 2>&1 | tail -8
objdump -d userland/desktop_qt/guest_link_compat.o | grep '46c7700' | head -2 || echo "WARN: holder VA not in compat.o"
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
pkill -9 -f qemu-system 2>/dev/null || true
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=480 bash tools/guest_desktop_smoke.sh > /tmp/v152-smoke.log 2>&1 || true
grep -E 'build=|futex|nanosleep|memcpy|QML ready|QQmlEngine ok|bridge|EXCEPTION|smoke\]|TIMEOUT|gui_shaders' /tmp/v152-smoke.log || true
f=$(ls -t /tmp/bfree-guest-smoke.* 2>/dev/null | head -1)
echo "LOG=$f"
grep -E 'build=|futex|nanosleep|memcpy|QML ready|QQmlEngine ok|bridge|qv4|EXCEPTION|event loop|gui_shaders|qrc init' "$f" | tail -80
