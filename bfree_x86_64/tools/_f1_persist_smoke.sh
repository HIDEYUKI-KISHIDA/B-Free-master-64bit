#!/usr/bin/env bash
# F1: /persist survives reboot via ATA-backed persist.img.
# Boot 1 writes /persist/f1; boot 2 (same img) must read it back.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
IMG=/tmp/bfree-f1-persist.img
ISO_STAGE=/tmp/bfree-f1-iso
ISO=/tmp/bfree-f1.iso
QLOG1=/tmp/bfree-f1-boot1.log
QLOG2=/tmp/bfree-f1-boot2.log

rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=8 status=none

rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/busybox_guest/busybox.elf "$ISO_STAGE/boot/busybox.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkf1.log 2>&1

run_boot() {
  local qlog="$1"; shift
  : > "$qlog"
  (
    sleep 70
    "$@"
    sleep 2
    printf 'echo DONE\n'
    sleep 2
  ) | timeout 120 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
      -drive file="$IMG",if=ide,index=0,media=disk,format=raw \
      -display none -serial mon:stdio >>"$qlog" 2>&1 || true
}

boot1_cmds() {
  printf 'echo F1_DATA_XYZ > /persist/f1\n'
  sleep 2
  printf 'cat /persist/f1\n'
  sleep 2
}
boot2_cmds() {
  printf 'cat /persist/f1\n'
  sleep 2
}

echo "=== boot 1: write ==="
run_boot "$QLOG1" boot1_cmds
echo "=== boot 2: read after reboot ==="
run_boot "$QLOG2" boot2_cmds

echo "=== F1 persist smoke ==="
fail=0
if grep -aq 'F1_DATA_XYZ' "$QLOG1"; then
  echo "PASS boot1_write"
else
  echo "FAIL boot1_write"
  fail=1
fi
if grep -aq 'F1_DATA_XYZ' "$QLOG2"; then
  echo "PASS boot2_persist (F1_PERSIST_OK)"
else
  echo "FAIL boot2_persist"
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG1" "$QLOG2"; then
  echo "FAIL panic"
  fail=1
else
  echo "PASS no_panic"
fi
echo '--- boot1 ---'
grep -aE 'PERSIST|persist|F1_DATA|Page Fault|PANIC' "$QLOG1" | tail -15
echo '--- boot2 ---'
grep -aE 'PERSIST|persist|F1_DATA|Page Fault|PANIC' "$QLOG2" | tail -15
exit "$fail"
