#!/usr/bin/env bash
set -eu
export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin
ELF="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/desktop.elf"
ADDR="${1:-0x31E752F}"
x86_64-elf-addr2line -f -C -e "$ELF" "$ADDR"
echo "--- callers (grep in libQt6Qml) ---"
x86_64-elf-nm -n "$ELF" | awk -v a="$(printf %d $ADDR)" '$1 ~ /^[0-9a-f]+$/ { if (strtonum("0x"$1) <= a) sym=$0 } END { print sym }'
