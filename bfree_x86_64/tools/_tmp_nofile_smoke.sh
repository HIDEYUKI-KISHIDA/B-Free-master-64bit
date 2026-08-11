#!/usr/bin/env bash
# RLIMIT_NOFILE enforce: ulimit -n 8 then open until EMFILE.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
mkdir -p "$ROOT/.cache"
pkill -9 -f qemu-system-x86_64 2>/dev/null || true
sleep 1

echo "[nofile] rebuild init AUTO_LOGIN+BOOT_BUSYBOX"
make -C userland/init clean >/dev/null
make -C userland/init BFREE_AUTO_LOGIN=1 BFREE_BOOT_BUSYBOX=1 -j2

STAGE="$ROOT/.cache/bfree-nofile-iso"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
cp -f kernel/kernel.elf "$STAGE/boot/kernel.elf"
cp -f iso_root/boot/busybox.elf "$STAGE/boot/busybox.elf"
cp -f userland/init/init.elf "$STAGE/boot/initrd.img"
cp -f iso_root/boot/grub/grub.cfg "$STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=3/' "$STAGE/boot/grub/grub.cfg"
ISO="$ROOT/.cache/bfree-nofile.iso"
grub-mkrescue -o "$ISO" "$STAGE" -- -volid BFREE >/dev/null 2>&1
qlog="$ROOT/.cache/bfree-nofile.log"
: >"$qlog"
(
  for _ in $(seq 1 90); do
    grep -q 'root@bfree' "$qlog" 2>/dev/null && break
    sleep 1
  done
  sleep 2
  # Soft limit 8: fds 0..7 max; opening /dev/null in a loop should hit EMFILE.
  printf '%s\n' 'ulimit -n 8'
  sleep 1
  # Keep fds open: with soft=8, fd numbers >=8 must EMFILE.
  printf '%s\n' 'i=3; while [ $i -lt 20 ]; do eval "exec $i>/dev/null" || { echo NOFILE_EMFILE_OK; break; }; i=$((i+1)); done; echo NOFILE_LOOP_DONE'
  sleep 8
) | timeout 200 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ISO" \
  -display none -serial mon:stdio >>"$qlog" 2>&1 || true
strings -n 4 "$qlog" > "$ROOT/.cache/bfree-nofile-str.txt"
echo "=== nofile markers ==="
grep -E 'NOFILE_|root@|ulimit|EMFILE|PANIC|EXCEPTION' "$ROOT/.cache/bfree-nofile-str.txt" | head -40 || true
if grep -aq 'NOFILE_EMFILE_OK' "$ROOT/.cache/bfree-nofile-str.txt"; then
  echo 'RESULT=PASS rlimit_nofile'
  exit 0
fi
echo 'RESULT=FAIL rlimit_nofile'
exit 1
