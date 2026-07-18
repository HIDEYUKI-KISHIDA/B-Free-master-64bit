#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
cd "$ROOT"
for s in fix_v211_funcname_guest.py fix_v213_funcproto_guest.py fix_v214_revert_init_guest.py \
  fix_v215_postfunc_guest.py fix_v216_generator_regexp_guest.py fix_v217_error_guest.py \
  fix_v218_objectproxy_guest.py fix_v219_objectproto_guest.py fix_v220_variant_sequence_guest.py \
  fix_v221_ctors_guest.py fix_v222_ctor_heartbeats_guest.py fix_v223_skip_regexp_error_ctors_guest.py \
  fix_v224_iterator_guest.py fix_v225_proto_init_skip_guest.py fix_v226_ctor_finish_guest.py \
  fix_v227_qqmlengine_guest.py fix_v228_setqmlengine_guest.py; do
  python3 "tools/$s"
done
bash tools/fix_asproto_guest.sh
bash tools/patch_qv4object_guest.sh
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/qml/qqmlengine.cpp.o"
cmake --build "$BD" --target Qml -j12
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
sed -i 's/mmap96-v[0-9]*/mmap96-v228/' userland/desktop_qt/guest_main.cpp
rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
export TMPDIR=/tmp
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf
rm -f userland/desktop_qt/guest_link_compat.o userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
strings iso_root/boot/desktop.elf | grep 'mmap96-v' | head -1
echo "[v228] done"
