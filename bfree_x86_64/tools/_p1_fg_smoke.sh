#!/usr/bin/env bash
# H06 ash fg/bg smoke: sleep & → jobs → fg (kernel jobctl path).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
ISO_STAGE=/tmp/bfree-p1fg-iso
QLOG=/tmp/bfree-p1fg.log
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/busybox_guest/busybox.elf "$ISO_STAGE/boot/busybox.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o /tmp/bfree-p1fg.iso "$ISO_STAGE" -- -volid BFREE >/tmp/mkp1fg.log 2>&1
: > "$QLOG"
(
  sleep 70
  printf 'sleep 2 &\n'
  sleep 2
  printf 'jobs\n'
  sleep 2
  printf 'fg\n'
  sleep 4
  printf 'echo P1_FG_SMOKE_O""K\n'
  sleep 2
  printf 'echo DONE\n'
  sleep 2
) | timeout 120 qemu-system-x86_64 -m 512M -no-reboot -cdrom /tmp/bfree-p1fg.iso \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true
echo "=== P1 ash fg smoke ==="
if grep -q 'P1_FG_SMOKE_OK' "$QLOG"; then
  echo "PASS P1_FG_SMOKE_OK"
else
  echo "FAIL P1_FG_SMOKE_OK"
fi
if grep -qiE 'Page Fault|PANIC' "$QLOG"; then
  echo "FAIL panic"
else
  echo "PASS no_panic"
fi
echo '--- relevant ---'
grep -aE 'jobs|fg|sleep|P1_FG|Page Fault|PANIC|root@' "$QLOG" | tail -40
