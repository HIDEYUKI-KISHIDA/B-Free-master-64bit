#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
mkdir -p "$ROOT/.cache"
pkill -9 -f qemu-system-x86_64 2>/dev/null || true
sleep 1

echo "[p8flock] rebuild init AUTO_LOGIN+BOOT_BUSYBOX"
make -C userland/init clean >/dev/null
make -C userland/init BFREE_AUTO_LOGIN=1 BFREE_BOOT_BUSYBOX=1 -j2
strings userland/init/init.elf | grep -F 'busybox.elf' | head -3

STAGE="$ROOT/.cache/bfree-p8flock-iso"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
cp -f kernel/kernel.elf "$STAGE/boot/kernel.elf"
cp -f iso_root/boot/busybox.elf "$STAGE/boot/busybox.elf"
cp -f userland/init/init.elf "$STAGE/boot/initrd.img"
cp -f iso_root/boot/p8test.elf "$STAGE/boot/p8test.elf"
cp -f iso_root/boot/grub/grub.cfg "$STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$STAGE/boot/grub/grub.cfg"
if ! grep -q 'p8test.elf' "$STAGE/boot/grub/grub.cfg"; then
  sed -i '/module2 \/boot\/busybox.elf busybox.elf/a\    module2 /boot/p8test.elf p8test.elf' \
    "$STAGE/boot/grub/grub.cfg"
fi
ISO="$ROOT/.cache/bfree-p8flock.iso"
grub-mkrescue -o "$ISO" "$STAGE" -- -volid BFREE >/dev/null 2>&1
qlog="$ROOT/.cache/bfree-p8flock.log"
: >"$qlog"
(
  for _ in $(seq 1 90); do
    grep -q 'root@bfree' "$qlog" 2>/dev/null && break
    sleep 1
  done
  sleep 2
  printf '/p8test.elf\n'
  sleep 35
) | timeout 200 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
  -display none -serial mon:stdio >>"$qlog" 2>&1 || true
strings -n 4 "$qlog" > "$ROOT/.cache/bfree-p8flock-str.txt"
echo "=== flock markers ==="
grep -E 'P8_FLOCK|P8_VAR|root@|busybox|PANIC|EXCEPTION|Page Fault' "$ROOT/.cache/bfree-p8flock-str.txt" | head -40 || true
if grep -aq 'P8_FLOCK_OK' "$ROOT/.cache/bfree-p8flock-str.txt"; then
  echo 'RESULT=PASS p8_flock'
  exit 0
fi
echo 'RESULT=FAIL p8_flock'
exit 1
