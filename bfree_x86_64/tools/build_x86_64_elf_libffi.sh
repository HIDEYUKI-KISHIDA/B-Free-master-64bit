#!/usr/bin/env bash
# Cross-build libffi for x86_64-elf (Wayland / QtWayland guest dependency).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/resolve_elf_musl_paths.sh
source "$ROOT/tools/resolve_elf_musl_paths.sh"
export_elf_musl_paths "$ROOT"
ensure_x86_64_elf_toolchain_path "$ROOT"

PREFIX="$(resolve_elf_out_prefix BFREE_ELF_LIBFFI_DIR x86_64-elf-libffi "$ROOT")"
FFI_VER="${BFREE_LIBFFI_VERSION:-3.4.6}"
SRC="${BFREE_LIBFFI_SRC:-$HOME/src/libffi-${FFI_VER}}"
MUSL_SYSROOT="$BFREE_ELF_MUSL_SYSROOT"
LIBC_A="$BFREE_ELF_LIBM_DIR/libc.a"

if [[ -f "$PREFIX/lib/libffi.a" && -f "$PREFIX/include/ffi.h" ]]; then
  echo "[elf-libffi] already built: $PREFIX"
  exit 0
fi

if [[ ! -f "$MUSL_SYSROOT/include/stdio.h" ]]; then
  echo "[elf-libffi] musl sysroot missing at $MUSL_SYSROOT — building ..."
  bash "$ROOT/tools/build_x86_64_elf_libm.sh"
  export_elf_musl_paths "$ROOT"
  MUSL_SYSROOT="$BFREE_ELF_MUSL_SYSROOT"
  LIBC_A="$BFREE_ELF_LIBM_DIR/libc.a"
fi
[[ -f "$LIBC_A" ]] || {
  echo "[elf-libffi] missing $LIBC_A" >&2
  echo "  export BFREE_ELF_LIBM_DIR=\$HOME/out/x86_64-elf-libm" >&2
  exit 1
}

if [[ ! -f "$SRC/configure" ]]; then
  mkdir -p "$(dirname "$SRC")"
  echo "[elf-libffi] fetching libffi $FFI_VER ..."
  curl -fsSL "https://github.com/libffi/libffi/releases/download/v${FFI_VER}/libffi-${FFI_VER}.tar.gz" \
    | tar -xz -C "$(dirname "$SRC")"
fi

export BFREE_ELF_MUSL_SYSROOT="$MUSL_SYSROOT"
GCC_WRAP="$ROOT/tools/x86_64-elf-gcc-meson-wrap.sh"

rm -rf "$SRC/x86_64-pc-elf" "$PREFIX"
mkdir -p "$PREFIX"
echo "[elf-libffi] configure -> $PREFIX (sysroot=$MUSL_SYSROOT)"
(
  cd "$SRC"
  ./configure --host=x86_64-elf --prefix="$PREFIX" --disable-shared --enable-static \
    CC="$GCC_WRAP" AR=x86_64-elf-ar RANLIB=x86_64-elf-ranlib \
    CFLAGS="-O2" LDFLAGS="-nostdlib -L$MUSL_SYSROOT/lib $LIBC_A"
  make -j"$(nproc 2>/dev/null || echo 4)"
  make install
)

if [[ ! -f "$PREFIX/lib/libffi.a" ]]; then
  echo "[elf-libffi] ERROR: install missing libffi.a" >&2
  exit 1
fi
echo "[elf-libffi] OK: $PREFIX/lib/libffi.a"
