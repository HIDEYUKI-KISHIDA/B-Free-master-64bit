#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
cd "$ROOT"
for f in \
    tools/patch_qv4internalclass_namemap_add_guest.sh \
    tools/patch_qv4internalclass_ptr_set_guest.sh \
    tools/patch_qv4namemap_sanitize_guest.sh \
    tools/patch_qv4addmember_init_order_guest.sh \
    tools/patch_qv4propertydata_guest.sh \
    tools/patch_qv4mm_allocobject_memberdata_guest.sh \
    tools/patch_qv4mm_allocdata_guest.sh \
    tools/patch_qv4engine_arrayproto_guest.sh; do
  sed -i 's/\r$//' "$f"
  bash "$f" || echo "[v197] warn: $f -> $?"
done
rm -f "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o"
cmake --build "$BD" --target Qml -j8
cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf
make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1 || true
BFREE_SMOKE_REBUILD=0 BFREE_FAST_TIMEOUT=300 bash tools/guest_desktop_smoke.sh 2>&1 | tee /tmp/v197-smoke.log | tail -80
