#!/usr/bin/env bash
# Build B2.20 hello.elf: prefer musl-static; fallback freestanding raw.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/userland/musl_hello"
mkdir -p "$OUT"
export PATH="/root/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

built=0
if command -v musl-gcc >/dev/null 2>&1; then
  if musl-gcc -static -O2 -o "$OUT/hello.elf" "$OUT/hello.c"; then
    built=1
    echo "[hello] musl-static OK"
  fi
fi
if [[ "$built" != "1" ]]; then
  x86_64-elf-gcc -m64 -ffreestanding -nostdlib -fno-stack-protector -O2 \
    -c -o "$OUT/hello_raw.o" "$OUT/hello_raw.c"
  x86_64-elf-ld -melf_x86_64 -e _start -Ttext=0x400000 -o "$OUT/hello.elf" "$OUT/hello_raw.o"
  echo "[hello] freestanding fallback OK"
fi
cp -f "$OUT/hello.elf" "$ROOT/iso_root/boot/musl_hello.elf"
cp -f "$OUT/hello.elf" "$ROOT/iso_root/boot/hello.elf"
echo "MUSL_HELLO_OK size=$(stat -c%s "$OUT/hello.elf")"
