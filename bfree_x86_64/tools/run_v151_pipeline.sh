#!/usr/bin/env bash
# v151: futex/nanosleep trace, QString patch, libstdc++ gthreads, Qml rebuild, smoke.
set -eu
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
cd "$ROOT"

sed -i 's/\r$//' tools/patch_qstring_guest.sh tools/patch_qlocale_guest.sh

echo "=== 1-2: Qt patches ==="
bash tools/patch_qlocale_guest.sh
bash tools/patch_qstring_guest.sh

echo "=== 3: libstdc++ gthreads ==="
if bash tools/force_libstdcxx_gthreads.sh 2>&1 | tail -5; then
  echo "[v151] libstdc++ gthreads ok"
else
  echo "[v151] libstdc++ gthreads skipped/failed (continuing)"
fi

echo "=== Qt Core rebuild ==="
touch /root/src/qt6/qtbase/src/corelib/text/qstring.cpp
cd /root/out/bfree-qt6-guest-static/build-qtbase
cmake --build . --target Core -j4 2>&1 | tail -6
cp -a lib/libQt6Core.a /root/out/bfree-qt6-guest-static/lib/

echo "=== Qt Qml single-thread rebuild ==="
bash "$ROOT/tools/rebuild_guest_qtdeclarative_singlethread.sh" 2>&1 | tail -8

echo "=== desktop.elf ==="
cd "$ROOT"
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf 2>&1 | tail -6

cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
pkill -9 -f qemu-system 2>/dev/null || true

echo "=== smoke 480s ==="
BFREE_SMOKE_REBUILD=0 BFREE_SMOKE_TIMEOUT=480 bash tools/guest_desktop_smoke.sh > /tmp/v151-smoke.log 2>&1 || true
grep -E 'build=|futex|nanosleep|ppoll|memcpy|QML ready|QQmlEngine ok|bridge|EXCEPTION|smoke\]|TIMEOUT' /tmp/v151-smoke.log || true
f=$(ls -t /tmp/bfree-guest-smoke.* 2>/dev/null | head -1)
echo "LOG=$f"
grep -E 'build=|futex|nanosleep|ppoll|memcpy|QML ready|QQmlEngine ok|bridge|qv4|EXCEPTION|event loop' "$f" | tail -80
