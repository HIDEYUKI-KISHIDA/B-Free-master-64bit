#!/usr/bin/env bash
# Fast QML guest iteration: incremental Qml/desktop rebuild + xorriso ISO + early-exit smoke.
#
# Usage:
#   bash tools/guest_qml_fast_iterate.sh              # patches + qml + elf + iso + smoke
#   bash tools/guest_qml_fast_iterate.sh smoke        # smoke only (existing ISO)
#   bash tools/guest_qml_fast_iterate.sh elf          # desktop.elf + iso only
#   bash tools/guest_qml_fast_iterate.sh qml          # libQt6Qml.a + elf + iso + smoke
#
# Env:
#   BFREE_FAST_JOBS=N          parallel cmake jobs (default: nproc)
#   BFREE_FAST_TIMEOUT=180     smoke seconds (default 180; faults exit in ~30-60s)
#   BFREE_FAST_SYMBOLIZE=0     skip addr2line on fail (default 0 = fast)
#   BFREE_FAST_PATCH=1         run patch scripts (default 1)
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
BD="/root/out/bfree-qt6-guest-static/build-qtdeclarative"
JOBS="${BFREE_FAST_JOBS:-$(nproc 2>/dev/null || echo 4)}"
TIMEOUT="${BFREE_FAST_TIMEOUT:-180}"
STAGE="${1:-all}"

cd "$ROOT"

kill_stale_qemu() {
  pkill -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
}

run_patches() {
  [[ "${BFREE_FAST_PATCH:-1}" == "1" ]] || return 0
  local scripts=(
    tools/patch_qv4internalclass_guest.sh
    tools/patch_qv4internalclass_revert_vtable_guest.sh
    tools/patch_qv4internalclass_inplace_guest.sh
    tools/patch_qv4internalclass_addmember_guest.sh
    tools/patch_qv4internalclass_addmember_entry_guest.sh
    tools/patch_qv4propertyhash_guest.sh
    tools/patch_qv4propertyhash_entries_guest.sh
    tools/patch_qv4internalclass_init_other_guest.sh
    tools/patch_qv4internalclass_grow_guest.sh
    tools/patch_qv4internalclass_namemap_add_guest.sh
    tools/patch_qv4internalclass_ptr_set_guest.sh
    tools/patch_qv4namemap_heartbeat_guest.sh
    tools/patch_qv4namemap_sanitize_guest.sh
    tools/patch_qv4addmember_init_order_guest.sh
    tools/patch_qv4propertydata_guest.sh
    tools/patch_qv4addmember_heartbeat_guest.sh
    tools/patch_qv4engine_newclass_zero_guest.sh
    tools/patch_qv4engine_guest.sh
    tools/inject_qv4engine_active.sh
    tools/fix_asproto_guest.sh
    tools/patch_qv4object_guest.sh
    tools/patch_qv4engine_fix_classobject_block_guest.sh
    tools/patch_qv4engine_newinternalclass_guest.sh
    tools/patch_qv4engine_fix_string_block_guest.sh
    tools/patch_qv4engine_add_scope_ic_guest.sh
    tools/patch_qv4engine_newidentifier_guest.sh
    tools/patch_qv4writebarrier_markcustom_guest.sh
    tools/patch_qv4symbol_create_guest.sh
    tools/patch_qv4symbol_init_guest.sh
    tools/patch_qv4engine_jsstrings_heartbeat_guest.sh
    tools/patch_qv4engine_jsstrings_value_guest.sh
    tools/patch_qv4engine_guest_propertykey.sh
    tools/patch_qv4engine_arrayproto_guest.sh
    tools/patch_qv4mm_allocate_guest.sh
    tools/patch_qv4mm_allocmanaged_ic_null_guest.sh
    tools/patch_qv4mm_allocobject_memberdata_guest.sh
    tools/patch_qv4mm_allocdata_guest.sh
    tools/patch_qv4memberdata_allocate_guest.sh
  )
  for f in "${scripts[@]}"; do
    [[ -f "$f" ]] || continue
    sed -i 's/\r$//' "$f"
    bash "$f" || echo "[fast] warn: $f returned $?"
  done
}

build_qml() {
  echo "[fast] Qml -j${JOBS} (incremental)"
  rm -f \
    "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4engine.cpp.o" \
    "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4internalclass.cpp.o" \
    "$BD/src/qml/CMakeFiles/Qml.dir/jsruntime/qv4object.cpp.o"
  cmake --build "$BD" --target Qml -j"${JOBS}"
  cp -a "$BD/lib/libQt6Qml.a" /root/out/bfree-qt6-guest-static/lib/
}

build_elf() {
  echo "[fast] desktop.elf (incremental link)"
  rm -f userland/desktop_qt/guest_main.o userland/desktop_qt/desktop.elf
  if [[ -f userland/desktop_qt/Makefile.guest-elf ]]; then
    make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
  else
    make -f userland/desktop_qt/Makefile.bfree guest-elf
  fi
  bash tools/update_guest_resource_holder_va.sh userland/desktop_qt/desktop.elf
  make -f userland/desktop_qt/Makefile.bfree guest-elf-fast
}

update_iso() {
  cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
  if [[ "${BFREE_FORCE_GRUB_ISO:-0}" == "1" ]]; then
    echo "[fast] ISO via grub-mkrescue"
    grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
  else
    bash tools/fast_update_iso_desktop.sh || {
      echo "[fast] xorriso failed; grub fallback"
      grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/dev/null 2>&1
    }
  fi
}

run_smoke() {
  kill_stale_qemu
  BFREE_SMOKE_REBUILD=0 \
  BFREE_SMOKE_TIMEOUT="${TIMEOUT}" \
  BFREE_SMOKE_NO_SYMBOLIZE="${BFREE_FAST_SYMBOLIZE:-0}" \
  BFREE_SMOKE_POLL_MS="${BFREE_SMOKE_POLL_MS:-150}" \
  bash tools/guest_desktop_smoke.sh 2>&1 | tee "/tmp/bfree-fast-smoke.$$.log"
}

case "$STAGE" in
  patch)  run_patches ;;
  qml)    run_patches; build_qml; build_elf; update_iso; run_smoke ;;
  elf)    build_elf; update_iso ;;
  iso)    update_iso ;;
  smoke)  run_smoke ;;
  all|*)  run_patches; build_qml; build_elf; update_iso; run_smoke ;;
esac
