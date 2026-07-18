#!/usr/bin/env bash
set -eu
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
cd "$ROOT"
for f in tools/rebuild_qml_stacklimits_guest.sh tools/rebuild_guest_qtdeclarative_singlethread.sh; do
  sed -i 's/\r$//' "$f"
done

echo "=== QV4 stacklimits BFREE_GUEST_FIXED_STACK ==="
bash tools/rebuild_qml_stacklimits_guest.sh 2>&1 | tail -6

echo "=== qtdeclarative single-thread (reconfigure) ==="
bash tools/rebuild_guest_qtdeclarative_singlethread.sh 2>&1 | tail -8

echo "=== desktop.elf v155 ==="
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf 2>&1 | tail -5

cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
pkill -9 -f qemu-system 2>/dev/null || true

echo "=== smoke 600s ==="
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=600 bash tools/guest_desktop_smoke.sh > /tmp/v155-smoke.log 2>&1 || true
grep -E 'build=|QML ready|QQmlEngine ok|bridge|load Desktop|EXCEPTION|smoke\]|TIMEOUT|readlink|realpath' /tmp/v155-smoke.log || true
f=$(ls -t /tmp/bfree-guest-smoke.* | head -1)
echo "SERIAL=$f"
grep -E 'build=|QML ready|QQmlEngine ok|bridge|load Desktop|qv4|EXCEPTION|event loop|readlink|realpath|qrc init' "$f" | tail -70
