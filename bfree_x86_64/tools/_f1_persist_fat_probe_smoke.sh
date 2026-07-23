#!/usr/bin/env bash
# F1 FAT scaffold: freestanding open("/persist") → kernel FAT BPB probe UART.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"
CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"
IMG=/tmp/bfree-f1-fat.img
ISO_STAGE=/tmp/bfree-f1fat-iso
ISO=/tmp/bfree-f1fat.iso
QLOG=/tmp/bfree-f1fat.log

bash tools/_f1_persist_fat_img.sh "$IMG"
touch kernel/sysmain/main.c
make -C kernel BFREE_PERSIST_FAT_PROBE=1 -j4 2>&1 | tail -12

cat > /tmp/bfree_f1_fat_probe.c <<'EOF'
#include <stdint.h>
#define SYS_write 1
#define SYS_open 2
#define SYS_exit 60
#define O_RDONLY 0
static long sys3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}
static void ser(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    (void)sys3(SYS_write, 1, (long)s, (long)(p - s));
}
void _start(void)
{
    long fd = sys3(SYS_open, (long)"/persist", O_RDONLY, 0);
    if (fd < 0) {
        ser("[f1fat] FAIL open\n");
        sys3(SYS_exit, 1, 0, 0);
    }
    ser("[f1fat] OPEN_OK\n");
    for (;;) { __asm__ volatile("pause"); }
}
EOF

"$CC" -m64 -ffreestanding -fno-stack-protector -fno-pic -nostdlib \
  -o userland/f1_fat_probe.elf /tmp/bfree_f1_fat_probe.c \
  -Wl,-Ttext=0x2800000 -Wl,-e,_start

rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -2
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 userland/f1_fat_probe.elf "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >/tmp/mkf1fat.log 2>&1

: > "$QLOG"
timeout 70 qemu-system-x86_64 -m 512M -no-reboot -boot d -cdrom "$ISO" \
    -drive file="$IMG",if=ide,index=0,media=disk,format=raw \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

echo '=== F1 FAT BPB probe ==='
fail=0
if grep -aq 'FAT BPB detected' "$QLOG"; then
  echo 'PASS FAT_BPB_PROBE'
else
  echo 'FAIL FAT_BPB_PROBE'
  fail=1
fi
if grep -aq 'OPEN_OK' "$QLOG"; then
  echo 'PASS OPEN_OK'
else
  echo 'FAIL OPEN_OK'
  fail=1
fi
if grep -aqiE 'Page Fault|PANIC' "$QLOG"; then
  echo 'FAIL panic'
  fail=1
else
  echo 'PASS no_panic'
fi
grep -aE 'PERSIST|FAT|f1fat|Page Fault|PANIC' "$QLOG" | tail -30
exit "$fail"
