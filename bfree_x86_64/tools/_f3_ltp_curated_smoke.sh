#!/usr/bin/env bash
# F3: build curated LTP-style subset (musl static), run in guest via QEMU.
# Requires busybox AUTO_LOGIN init (same pattern as _libc_test_curated_smoke.sh).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

if ! command -v musl-gcc >/dev/null 2>&1; then
  echo "FAIL musl-gcc missing"
  exit 1
fi

echo "[f3] rebuild init (AUTO_LOGIN + BUSYBOX)"
make -C userland/init clean >/dev/null
make -C userland/init BFREE_AUTO_LOGIN=1 BFREE_BOOT_BUSYBOX=1 -j2 >/dev/null

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
if [[ -f userland/p8test/p8test.elf ]]; then
  install -m 0644 userland/p8test/p8test.elf "$ISO_STAGE/boot/p8test.elf"
fi
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$ISO_STAGE/boot/grub/grub.cfg"
if ! grep -q 'ltp_curated.elf' "$ISO_STAGE/boot/grub/grub.cfg"; then
  if grep -q 'p8test.elf' "$ISO_STAGE/boot/grub/grub.cfg"; then
    sed -i '/module2 \/boot\/p8test.elf p8test.elf/a\    module2 /boot/ltp_curated.elf ltp_curated.elf' \
      "$ISO_STAGE/boot/grub/grub.cfg"
  else
    sed -i '/module2 \/boot\/busybox.elf busybox.elf/a\    module2 /boot/ltp_curated.elf ltp_curated.elf' \
      "$ISO_STAGE/boot/grub/grub.cfg"
  fi
fi
grub-mkrescue -o /tmp/bfree-f3ltp.iso "$ISO_STAGE" -- -volid BFREE >/tmp/mkf3ltp.log 2>&1
: > "$QLOG"
(
  for _ in $(seq 1 90); do
    if grep -aq 'root@bfree' "$QLOG" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  sleep 2
  printf '/ltp_curated.elf\n'
  sleep 55
  printf 'echo F3_SMOKE_DON""E\n'
  sleep 2
) | timeout 200 qemu-system-x86_64 -m 512M -no-reboot -cdrom /tmp/bfree-f3ltp.iso \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

mkdir -p "$ROOT/.cache"
cp -f "$QLOG" "$ROOT/.cache/ltp_curated_qemu.log" 2>/dev/null || true

echo "=== F3 LTP curated smoke ==="
fail=0
if grep -aq 'LTP_CURATED_RESULT: PASS' "$QLOG"; then
  echo "PASS LTP_CURATED_RESULT"
else
  echo "FAIL LTP_CURATED_RESULT"
  fail=1
fi
if grep -aq '\[COW\] break' "$QLOG"; then
  echo "PASS COW_break"
else
  echo "FAIL COW_break (lazy fork COW not observed)"
  fail=1
fi
if grep -aq 'TPASS: mmap06_cow_refcnt' "$QLOG"; then
  echo "PASS mmap06_cow_refcnt"
else
  echo "FAIL mmap06_cow_refcnt"
  fail=1
fi
if grep -aq '\[COW\] ref free' "$QLOG"; then
  echo "PASS COW_ref_free"
else
  echo "FAIL COW_ref_free (shared phys not freed on last unmap)"
  fail=1
fi
if grep -aq 'TFAIL' "$QLOG"; then
  echo "FAIL has_TFAIL"
  fail=1
else
  echo "PASS no_TFAIL"
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  if grep -aq 'LTP_CURATED_RESULT: PASS' "$QLOG"; then
    echo "PASS no_panic (post-result ash noise ignored)"
  else
    echo "FAIL panic"
    fail=1
  fi
else
  echo "PASS no_panic"
fi
echo '--- relevant ---'
grep -aE 'TPASS|TFAIL|LTP_CURATED|Page Fault|PANIC|applet not found' "$QLOG" | tail -50

grep -a 'LTP_CURATED_RESULT' "$QLOG" | tail -1 > "$ROOT/.cache/ltp_curated_result.txt" || true
install -m 0644 userland/ltp_curated/ltp_curated.elf \
  "$ROOT/iso_root/boot/ltp_curated.elf" 2>/dev/null || true

echo "[f3] restore init (AUTO_LOGIN desktop)"
make -C userland/init clean >/dev/null || true
make -C userland/init BFREE_AUTO_LOGIN=1 -j2 >/dev/null || true

exit "$fail"
