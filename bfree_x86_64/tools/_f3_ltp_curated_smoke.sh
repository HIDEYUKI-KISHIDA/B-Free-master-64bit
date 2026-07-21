#!/usr/bin/env bash
# F3: build curated LTP-style subset (musl static), run in guest via QEMU.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

if ! command -v musl-gcc >/dev/null 2>&1; then
  echo "FAIL musl-gcc missing"
  exit 1
fi

echo "[f3] build ltp_curated"
make -C userland/ltp_curated clean
make -C userland/ltp_curated

ISO_STAGE=/tmp/bfree-f3ltp-iso
QLOG=/tmp/bfree-f3ltp.log
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/busybox_guest/busybox.elf "$ISO_STAGE/boot/busybox.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/ltp_curated/ltp_curated.elf "$ISO_STAGE/boot/ltp_curated.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$ISO_STAGE/boot/grub/grub.cfg"
if ! grep -q 'ltp_curated.elf' "$ISO_STAGE/boot/grub/grub.cfg"; then
  sed -i '/module2 \/boot\/busybox.elf busybox.elf/a\    module2 /boot/ltp_curated.elf ltp_curated.elf' \
    "$ISO_STAGE/boot/grub/grub.cfg"
fi
grub-mkrescue -o /tmp/bfree-f3ltp.iso "$ISO_STAGE" -- -volid BFREE >/tmp/mkf3ltp.log 2>&1
: > "$QLOG"
(
  sleep 70
  printf '/ltp_curated.elf\n'
  sleep 10
  printf 'echo F3_SMOKE_DON""E\n'
  sleep 2
) | timeout 130 qemu-system-x86_64 -m 512M -no-reboot -cdrom /tmp/bfree-f3ltp.iso \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo "=== F3 LTP curated smoke ==="
fail=0
if grep -aq 'LTP_CURATED_RESULT: PASS' "$QLOG"; then
  echo "PASS LTP_CURATED_RESULT"
else
  echo "FAIL LTP_CURATED_RESULT"
  fail=1
fi
if grep -aq 'TFAIL' "$QLOG"; then
  echo "FAIL has_TFAIL"
  fail=1
else
  echo "PASS no_TFAIL"
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo "FAIL panic"
  fail=1
else
  echo "PASS no_panic"
fi
echo '--- relevant ---'
grep -aE 'TPASS|TFAIL|LTP_CURATED|Page Fault|PANIC|applet not found' "$QLOG" | tail -30

# Record result for ltp_subset_gate.sh.
mkdir -p "$ROOT/.cache"
grep -a 'LTP_CURATED_RESULT' "$QLOG" | tail -1 > "$ROOT/.cache/ltp_curated_result.txt" || true
exit "$fail"
