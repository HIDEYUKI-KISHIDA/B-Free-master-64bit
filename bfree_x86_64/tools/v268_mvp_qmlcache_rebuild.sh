#!/usr/bin/env bash
# MVP qmlcache route: register precompiled GuestMvpShell + guest Qt patches + desktop.elf
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
PREFIX="/root/out/bfree-qt6-guest-static"
cd "$ROOT"
sed -i 's/\r$//' tools/v268_mvp_qmlcache_rebuild.sh tools/rebuild_guest_qml_after_patch.sh \
  tools/gen_guest_mvp_qmlcache.sh \
  tools/fix_v273*.py tools/fix_v274*.py tools/fix_v275*.py tools/fix_v271*.py
bash tools/gen_guest_mvp_qmlcache.sh
python3 tools/fix_v274_cached_unit_skip_verify_header_guest.py
python3 tools/fix_v273_precached_skip_typecompile_guest.py
python3 tools/fix_v275_remove_verifyCaches_assert_guest.py
python3 tools/fix_v271_skip_script_filename_guest.py || true
bash tools/rebuild_guest_qml_after_patch.sh
sed -i 's/mmap96-v[0-9a-z]*/mmap96-v268/' userland/desktop_qt/guest_main.cpp
rm -f userland/desktop_qt/desktop.elf
bash tools/build_guest_desktop_elf.sh
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
if ! [[ -d /usr/lib/grub/i386-pc ]]; then
  echo "[v268-mvp-qmlcache] ERROR: grub-pc-bin missing (ISO will not BIOS-boot)" >&2
  echo "  apt install -y grub-pc-bin xorriso grub2-common" >&2
  exit 1
fi
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE
strings userland/desktop_qt/desktop.elf | grep 'mmap96-v' | head -1
echo "[v268-mvp-qmlcache] done"
