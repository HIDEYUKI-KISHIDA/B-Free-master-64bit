#!/bin/bash
# Relink desktop.elf with updated guest_link_compat.o (pthread→CLONE_THREAD wrap).
# Qt guest libs live under /root/out — run as WSL root:
#   wsl -u root bash tools/relink_desktop_compat.sh
set -euo pipefail
export PATH="/home/h_kis/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
export HOME=/home/h_kis
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
SRC_FALLBACK="${BFREE_DESKTOP_OBJ_FALLBACK:-/home/h_kis/bfree_build/userland/desktop_qt}"
MUSL_INC="$ROOT/out/x86_64-elf-libm/prefix/include"
export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
export BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
export BFREE_ELF_CXX_INCLUDE="${BFREE_ELF_CXX_INCLUDE:-/root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0}"
export BFREE_ROOT="$ROOT"

test -f "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a"
test -f "$MUSL_INC/stdio.h"
test -f "$DESK/Makefile.guest-elf"

cd "$DESK"

echo "[1] restore empty/missing .o from $SRC_FALLBACK (except compat)"
if [ -d "$SRC_FALLBACK" ]; then
  for f in "$SRC_FALLBACK"/*.o; do
    base=$(basename "$f")
    [ "$base" = "guest_link_compat.o" ] && continue
    if [ ! -s "$DESK/$base" ]; then
      echo "  restore $base"
      cp -f "$f" "$DESK/$base"
    fi
  done
fi

echo "[2] build guest_link_compat.o (clone wrap)"
x86_64-elf-g++ -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-stack-check \
  -fno-stack-clash-protection -fno-pic \
  -Wall -Wextra -mno-sse -mno-mmx -mno-3dnow -fno-exceptions -fno-rtti -Wa,--noexecstack \
  -isystem "$MUSL_INC" -D_GNU_SOURCE \
  -x c++ -c -o "$DESK/guest_link_compat.o" "$ROOT/tools/guest_link_compat.cpp"
strings "$DESK/guest_link_compat.o" | grep -F 'pthread_create clone' >/dev/null
nm "$DESK/guest_main.o" | grep -q ' T main$'

echo "[3] link desktop.elf"
rm -f desktop desktop.elf
make -f Makefile.guest-elf desktop
if [ -f desktop ]; then mv -f desktop desktop.elf; fi
ls -la desktop.elf
strings desktop.elf | grep -qF 'pthread_create clone'
cp -f desktop.elf "$ROOT/iso_root/boot/desktop.elf"
echo "PASS desktop.elf relinked with pthread→clone wrap"
