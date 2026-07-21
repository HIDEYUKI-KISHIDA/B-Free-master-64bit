#!/usr/bin/env bash
# P3: build musl static hello.elf, Multiboot-load, run in QEMU.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

if ! command -v musl-gcc >/dev/null 2>&1; then
  echo "FAIL musl-gcc missing"
  exit 1
fi

echo "[p3] build musl_hello"
make -C userland/musl_hello clean
make -C userland/musl_hello

ISO_STAGE=/tmp/bfree-p3hello-iso
QLOG=/tmp/bfree-p3hello.log
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/busybox_guest/busybox.elf "$ISO_STAGE/boot/busybox.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/musl_hello/hello.elf "$ISO_STAGE/boot/hello.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$ISO_STAGE/boot/grub/grub.cfg"
# Ensure Multiboot module for hello.elf next to busybox.
if ! grep -q 'hello.elf' "$ISO_STAGE/boot/grub/grub.cfg"; then
  sed -i '/module2 \/boot\/busybox.elf busybox.elf/a\    module2 /boot/hello.elf hello.elf' \
    "$ISO_STAGE/boot/grub/grub.cfg"
fi
grub-mkrescue -o /tmp/bfree-p3hello.iso "$ISO_STAGE" -- -volid BFREE >/tmp/mkp3hello.log 2>&1
: > "$QLOG"
(
  sleep 70
  printf '/hello.elf\n'
  sleep 8
  printf 'echo P3_HELLO_SMOKE_O""K\n'
  sleep 2
) | timeout 120 qemu-system-x86_64 -m 512M -no-reboot -cdrom /tmp/bfree-p3hello.iso \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo "=== P3 musl hello smoke ==="
if grep -aq 'MUSL_HELLO_OK' "$QLOG"; then
  echo "PASS MUSL_HELLO_OK"
else
  echo "FAIL MUSL_HELLO_OK"
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo "FAIL panic"
else
  echo "PASS no_panic"
fi
echo '--- relevant ---'
grep -aE 'MUSL_HELLO|hello|Page Fault|PANIC|root@|applet not found' "$QLOG" | tail -40
