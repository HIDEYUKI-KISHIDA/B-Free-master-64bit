#!/usr/bin/env bash
# Build gthr_smoke.elf and boot briefly on current kernel.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/gthr_smoke.elf tools/gthr_smoke.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

ISO_STAGE=/tmp/bfree-gthr-iso
ISO=/tmp/bfree-gthr.iso
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
# Reuse init that auto-execs — rebuild init to run gthr_smoke if possible
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -3
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/gthr_smoke.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
sed -i '/busybox.elf/d;/p8test.elf/d' "$ISO_STAGE/boot/grub/grub.cfg" || true
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkgthr.log 2>&1

QLOG=/tmp/bfree-gthr.log
: > "$QLOG"
(
  sleep 45
  printf '\n'
  sleep 1
) | timeout 55 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true
tr -d '\r' <"$QLOG" >/tmp/bfree-gthr.nolog
echo '=== gthr ==='
grep -aE '\[gthr\]|Page Fault|PANIC|CLONE|eventfd' /tmp/bfree-gthr.nolog | tail -40
echo GTHR_DONE
