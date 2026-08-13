#!/usr/bin/env bash
# Install Linux kernel UAPI headers into musl guest sysroot (linux/, asm/, asm-generic/).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/resolve_elf_musl_paths.sh
source "$ROOT/tools/resolve_elf_musl_paths.sh"
export_elf_musl_paths "$ROOT"
MUSL_BASE="$BFREE_ELF_LIBM_DIR"
MUSL_PREFIX="$BFREE_ELF_MUSL_SYSROOT"
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

if ! mkdir -p "$INC/linux" "$INC/asm" "$INC/asm-generic" 2>/dev/null; then
  echo "[kernel-uapi] cannot write under $INC (permission denied?)" >&2
  echo "  export BFREE_ELF_LIBM_DIR=\$HOME/out/x86_64-elf-libm" >&2
  echo "  or use a writable musl sysroot under \$HOME/out" >&2
  exit 1
fi
cp -a /usr/include/linux/. "$INC/linux/"
cp -a /usr/include/asm-generic/. "$INC/asm-generic/"
cp -a /usr/include/x86_64-linux-gnu/asm/. "$INC/asm/"
echo "[kernel-uapi] installed under $INC"
