#!/usr/bin/env bash
set -eu
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
sed -i 's/\r$//' tools/patch_qstring_guest.sh
bash tools/patch_qstring_guest.sh
touch /root/src/qt6/qtbase/src/corelib/text/qstring.cpp
cd /root/out/bfree-qt6-guest-static/build-qtbase
cmake --build . --target Core -j4 2>&1 | tail -3
cp -a lib/libQt6Core.a /root/out/bfree-qt6-guest-static/lib/
cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf 2>&1 | tail -4
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
pkill -9 -f qemu-system 2>/dev/null || true
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=480 bash tools/guest_desktop_smoke.sh > /tmp/v154-smoke.log 2>&1 || true
grep -E 'build=|QML ready|QQmlEngine ok|bridge|EXCEPTION|smoke\]|TIMEOUT|memcpy|futex|nanosleep|readlink|realpath' /tmp/v154-smoke.log || true
f=$(ls -t /tmp/bfree-guest-smoke.* | head -1)
echo "SERIAL=$f"
grep -E 'build=|QML ready|QQmlEngine ok|bridge|qv4|EXCEPTION|event loop|memcpy|futex|nanosleep|readlink|realpath|qrc init' "$f" | tail -60
