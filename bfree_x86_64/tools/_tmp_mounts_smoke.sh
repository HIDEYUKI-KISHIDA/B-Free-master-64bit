#!/usr/bin/env bash
# mount <-> /proc/mounts via p8test (busybox has no umount applet).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
mkdir -p "$ROOT/.cache"
pkill -9 -f qemu-system-x86_64 2>/dev/null || true
sleep 1

echo "[mounts] rebuild p8test + init"
make -C userland/p8test -j2
make -C userland/init clean >/dev/null
make -C userland/init BFREE_AUTO_LOGIN=1 BFREE_BOOT_BUSYBOX=1 -j2
cp -f userland/p8test/p8test.elf iso_root/boot/p8test.elf

STAGE="$ROOT/.cache/bfree-mounts-iso"
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
ISO="$ROOT/.cache/bfree-mounts.iso"
grub-mkrescue -o "$ISO" "$STAGE" -- -volid BFREE >/dev/null 2>&1
qlog="$ROOT/.cache/bfree-mounts.log"
: >"$qlog"
(
  for _ in $(seq 1 90); do
    grep -q 'root@bfree' "$qlog" 2>/dev/null && break
    sleep 1
  done
  sleep 2
  printf '/p8test.elf\n'
  sleep 40
) | timeout 220 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
  -display none -serial mon:stdio >>"$qlog" 2>&1 || true
strings -n 4 "$qlog" > "$ROOT/.cache/bfree-mounts-str.txt"
echo "=== mounts markers ==="
grep -E 'P8_MOUNTS|P8_FLOCK|root@|PANIC|EXCEPTION' "$ROOT/.cache/bfree-mounts-str.txt" | head -40 || true
if grep -aq 'P8_MOUNTS_OK' "$ROOT/.cache/bfree-mounts-str.txt"; then
  echo 'RESULT=PASS mounts_proc'
  exit 0
fi
echo 'RESULT=FAIL mounts_proc'
exit 1
