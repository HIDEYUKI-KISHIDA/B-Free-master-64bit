#!/usr/bin/env bash
# Install Linux kernel UAPI headers into musl guest sysroot (linux/, asm/, asm-generic/).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

MUSL_BASE="${BFREE_ELF_LIBM_DIR:-/root/out/x86_64-elf-libm}"
MUSL_PREFIX="$MUSL_BASE"
if [[ -d "$MUSL_BASE/prefix/include" ]]; then
  MUSL_PREFIX="$MUSL_BASE/prefix"
fi
INC="$MUSL_PREFIX/include"

kernel_uapi_ok() {
  [[ -f "$INC/linux/fs.h" && -f "$INC/asm/unistd.h" && -f "$INC/asm-generic/ioctl.h" ]]
}

if kernel_uapi_ok; then
  echo "[kernel-uapi] ok: $INC (linux + asm + asm-generic)"
  exit 0
fi

if [[ ! -f /usr/include/linux/fs.h ]] && command -v apt-get >/dev/null 2>&1; then
  echo "[kernel-uapi] installing linux-libc-dev ..."
  apt-get install -y linux-libc-dev >/dev/null 2>&1 || true
fi

for need in /usr/include/linux/fs.h /usr/include/asm-generic/ioctl.h /usr/include/x86_64-linux-gnu/asm/unistd.h; do
  if [[ ! -f "$need" ]]; then
    echo "[kernel-uapi] missing $need — run: apt install linux-libc-dev" >&2
    exit 1
  fi
done

mkdir -p "$INC/linux" "$INC/asm" "$INC/asm-generic"
cp -a /usr/include/linux/. "$INC/linux/"
cp -a /usr/include/asm-generic/. "$INC/asm-generic/"
cp -a /usr/include/x86_64-linux-gnu/asm/. "$INC/asm/"
echo "[kernel-uapi] installed under $INC"
