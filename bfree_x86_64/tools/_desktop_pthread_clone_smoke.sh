#!/usr/bin/env bash
# Boot Qt desktop.elf; expect serial "[wrap] pthread_create clone" from new wrap.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

ELF_SRC=userland/desktop_qt/desktop.elf
SZ=$(stat -c%s "$ELF_SRC")
if [[ "$SZ" -lt 1000000 ]]; then
  echo "ERROR: desktop.elf too small ($SZ)" >&2
  exit 1
fi
if ! strings "$ELF_SRC" | grep -qF 'pthread_create clone'; then
  echo "ERROR: desktop.elf missing clone wrap string — run tools/relink_desktop_compat.sh" >&2
  exit 1
fi

echo "[1] init AUTO_LOGIN + ISO (desktop.elf=${SZ})"
make -C kernel -j4 2>&1 | tail -3
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2

ISO_STAGE=/tmp/bfree-desktop-pt-iso
ISO=/tmp/bfree-desktop-pt.iso
QLOG=/tmp/bfree-desktop-pt.log
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 "$ELF_SRC" "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
sed -i '/busybox.elf/d;/p8test.elf/d' "$ISO_STAGE/boot/grub/grub.cfg" || true
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkdesktop-pt.log 2>&1

echo "[2] QEMU (~3 min, watch for wrap)"
: > "$QLOG"
(
  sleep 180
  printf '\n'
  sleep 2
) | timeout 200 qemu-system-x86_64 -m 1024M -no-reboot -cdrom "$ISO" \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo '=== desktop pthread clone ==='
fail=0
if grep -aqF '[wrap] pthread_create clone' "$QLOG"; then
  echo 'PASS wrap_pthread_create_clone'
else
  echo 'FAIL wrap_pthread_create_clone'
  fail=1
fi
# coop fallback is OK to see, but clone should appear first for real creates
if grep -aqF '[wrap] pthread_create' "$QLOG"; then
  echo 'PASS wrap_pthread_create_any'
else
  echo 'FAIL wrap_pthread_create_any (no pthread_create yet — boot may be early)'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE '\[wrap\] pthread|pthread_create|PANIC|Page Fault|desktop_qt|QV4|QML' "$QLOG" | tail -40
exit "$fail"
