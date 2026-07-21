#!/usr/bin/env bash
# H06 ash fg/bg smoke: sleep & → jobs → fg (AS-copy FORK_BG).
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
fail=0
if grep -aq 'No current job' "$QLOG"; then
  echo "FAIL No_current_job"
  fail=1
else
  echo "PASS no_No_current_job"
fi
# ash jobs typically prints "[1]+  Running" or "Done"
if grep -aE '\[1\]|\[[0-9]+\]' "$QLOG" | grep -aqv 'No current'; then
  echo "PASS jobs_listing"
else
  echo "FAIL jobs_listing"
  fail=1
fi
if grep -aq 'P1_FG_SMOKE_OK' "$QLOG"; then
  echo "PASS P1_FG_SMOKE_OK"
else
  echo "FAIL P1_FG_SMOKE_OK"
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo "FAIL panic"
  fail=1
else
  echo "PASS no_panic"
fi
echo '--- relevant ---'
grep -aE 'jobs|fg|sleep|P1_FG|Page Fault|PANIC|root@|No current|Running|Done' "$QLOG" | tail -50
exit "$fail"
