#!/usr/bin/env bash
# Skip linux/fs.h in Qt Core for musl guest (FICLONE fallback uses sys/ioctl.h).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
MUSL_PREFIX="${BFREE_ELF_LIBM_DIR:-/root/out/x86_64-elf-libm}"
[[ -d "$MUSL_PREFIX/prefix/include" ]] && MUSL_PREFIX="$MUSL_PREFIX/prefix"
f="$QT_SRC/qtbase/src/corelib/io/qfilesystemengine_unix.cpp"
marker="$QT_SRC/qtbase/.bfree_guest_linux_fs_h_patched"

cleanup_partial_kernel_headers() {
  local inc="$MUSL_PREFIX/include"
  if [[ -d "$inc/linux" || -d "$inc/asm" ]] && [[ ! -f "$inc/asm-generic/ioctl.h" ]]; then
    echo "[patch] removing incomplete kernel UAPI from $inc"
    rm -rf "$inc/linux" "$inc/asm"
  fi
}

[[ -f "$f" ]] || { echo "[patch] missing: $f" >&2; exit 1; }
cleanup_partial_kernel_headers

if [[ -f "$marker" ]] && ! grep -q '#  include <linux/fs.h>' "$f" && ! grep -qF '__has_include(<linux/fs.h>)' "$f"; then
  echo "[patch] ok: linux/fs.h skipped for musl guest"
  exit 0
fi

perl -0pi -e '
  s/#  if __has_include\(<linux\/fs\.h>\)\n#    include <linux\/fs\.h>\n#  endif\n//g;
  s/#  include <linux\/fs\.h>\n//g;
' "$f"
touch "$marker"
echo "[patch] ok: linux/fs.h removed (FICLONE uses sys/ioctl.h fallback)"
