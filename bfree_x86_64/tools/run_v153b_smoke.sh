#!/usr/bin/env bash
set -eu
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
strings userland/desktop_qt/desktop.elf | grep 'build=mmap96' | head -1
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
pkill -9 -f qemu-system 2>/dev/null || true
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=480 bash tools/guest_desktop_smoke.sh > /tmp/v153b-smoke.log 2>&1 || true
tail -40 /tmp/v153b-smoke.log
f=$(ls -t /tmp/bfree-guest-smoke.* | head -1)
echo "SERIAL=$f"
grep 'build=' "$f" | head -1
grep -E 'QML ready|QQmlEngine ok|bridge|EXCEPTION|qv4|futex|nanosleep|readlink|realpath|qrc init' "$f" | tail -50
