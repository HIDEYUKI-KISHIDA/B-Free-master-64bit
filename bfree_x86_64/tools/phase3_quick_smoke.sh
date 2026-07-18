#!/usr/bin/env bash
# Quick serial smoke after kernel-only rebuild (skips busybox rebuild).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

bfree_prepend_toolchain() {
  local gcc_root="$1" bin=""
  if [[ -x "$gcc_root/bin/x86_64-elf-gcc" ]]; then
    bin="$gcc_root/bin"
  elif [[ -x "$gcc_root/x86_64-elf/bin/x86_64-elf-gcc" ]]; then
    bin="$gcc_root/x86_64-elf/bin"
  else
    return 1
  fi
  export PATH="$bin:/usr/bin:/bin:${PATH:-}"
}

if [[ -x "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-gcc" ]]; then
  bfree_prepend_toolchain "${HOME}/x86_64-elf-toolchain"
elif [[ -x /root/x86_64-elf-toolchain/bin/x86_64-elf-gcc ]]; then
  bfree_prepend_toolchain /root/x86_64-elf-toolchain
fi

make -C kernel -j"$(nproc 2>/dev/null || echo 4)"
cp -f kernel/kernel.elf iso_root/boot/kernel.elf
if [[ ! -f iso_root/boot/busybox.elf ]]; then
  echo "missing iso_root/boot/busybox.elf — run tools/build_guest_busybox.sh first" >&2
  exit 1
fi
if [[ ! -f iso_root/boot/initrd.img ]]; then
  echo "missing iso_root/boot/initrd.img" >&2
  exit 1
fi

GRUB_CFG="$ROOT/iso_root/boot/grub/grub.cfg"
GRUB_BAK="$ROOT/.cache/grub.cfg.phase3.bak"
mkdir -p "$(dirname "$GRUB_BAK")"
cp -f "$GRUB_CFG" "$GRUB_BAK"
sed -i 's/^set default=.*/set default=3/' "$GRUB_CFG"
grub-mkrescue -o bfree.iso iso_root -- -volid BFREE >/tmp/bfree-quick-mkrescue.log 2>&1
cp -f "$GRUB_BAK" "$GRUB_CFG"

qlog=/tmp/bfree-quick.log
: >"$qlog"
pkill -9 -f 'qemu-system-x86_64.*bfree' 2>/dev/null || true
sleep 1

(
  sleep 90
  printf 'echo hello | cat\n'
  sleep 2
  printf 'echo GREP_PIPE_OK | grep GREP_PIPE_OK\n'
  sleep 3
  printf 'grep -e PATH /etc/profile\n'
  sleep 2
  printf 'echo PIPE_REDIR_OK > /tmp/p3 && cat /tmp/p3\n'
  sleep 2
  printf 'echo M1CP > /tmp/m1src\n'
  sleep 2
  printf 'cp /tmp/m1src /tmp/m1dst && cat /tmp/m1dst && echo ZCP_OK\n'
  sleep 3
  printf 'mv /tmp/m1dst /tmp/m1mv && cat /tmp/m1mv && echo ZMV_OK\n'
  sleep 3
  printf 'rm /tmp/m1mv /tmp/m1src && echo M1_RM_OK\n'
  sleep 2
  printf 'mkdir /tmp/m1dir\n'
  sleep 2
  printf 'rm -r /tmp/m1dir && echo M1_RMDIR_OK\n'
  sleep 2
  printf 'ps\n'
  sleep 3
  printf 'kill -l\n'
  sleep 2
) | timeout 280 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ROOT/bfree.iso" \
    -display none -serial mon:stdio >>"$qlog" 2>&1 || true

echo "==== quick smoke tail ===="
grep -E 'root@bfree|hello|GREP|PATH|PIPE_REDIR|ZCP_OK|ZMV_OK|M1_|Bad file|PANIC|Page Fault|applet not found|terminating' "$qlog" | tail -60

fail=0
grep -q 'GREP_PIPE_OK' "$qlog" || fail=1
grep -q 'export PATH=' "$qlog" || fail=1
grep -q 'PIPE_REDIR_OK' "$qlog" || fail=1
grep -q 'ZCP_OK' "$qlog" || fail=1
grep -q 'ZMV_OK' "$qlog" || fail=1
grep -q 'M1_RM_OK' "$qlog" || fail=1
grep -q 'M1_RMDIR_OK' "$qlog" || fail=1
grep -qE 'PANIC|Page Fault|applet not found' "$qlog" && fail=1
if [[ "$fail" -eq 0 ]]; then
  echo "QUICK RESULT: PASS"
else
  echo "QUICK RESULT: FAIL"
fi
exit "$fail"
