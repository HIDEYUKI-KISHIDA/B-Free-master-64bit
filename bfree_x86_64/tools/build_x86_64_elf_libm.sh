#!/usr/bin/env bash
# Build musl libc.a + libm.a for x86_64-elf (lordmilko zip has gcc/g++ only).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL_VER="${BFREE_MUSL_VERSION:-1.2.5}"
MUSL_SRC="${BFREE_MUSL_SRC:-/root/src/musl-${MUSL_VER}}"
OUT_DIR="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}"
INSTALL_LIBC="$OUT_DIR/libc.a"
INSTALL_LIBM="$OUT_DIR/libm.a"

if [[ -f "$ROOT/tools/ensure_x86_64_elf_toolchain.sh" ]]; then
  export PATH="/root/x86_64-elf-toolchain/bin:${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
  BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh") || true
fi

CC="${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc 2>/dev/null || true)}"
AR="${BFREE_ELF_AR:-$(command -v x86_64-elf-ar 2>/dev/null || true)}"
CROSS="${BFREE_CROSS_PREFIX:-x86_64-elf-}"

[[ -n "$CC" && -x "$CC" ]] || { echo "[elf-musl] x86_64-elf-gcc not found (export PATH=/root/x86_64-elf-toolchain/bin:\$PATH)" >&2; exit 1; }
echo "[elf-musl] using CC=$CC"
[[ -x "$AR" ]] || AR="${CROSS}ar"

if [[ -f "$INSTALL_LIBC" && -f "$INSTALL_LIBM" ]]; then
  echo "[elf-musl] already built: $INSTALL_LIBC $INSTALL_LIBM"
  exit 0
fi

if [[ ! -f "$MUSL_SRC/Makefile" ]]; then
  echo "[elf-musl] fetching musl $MUSL_VER into $(dirname "$MUSL_SRC") ..."
  mkdir -p "$(dirname "$MUSL_SRC")"
  tmp="$(mktemp -d)"
  wget -q -O "$tmp/musl.tar.gz" "https://musl.libc.org/releases/musl-${MUSL_VER}.tar.gz"
  tar -xzf "$tmp/musl.tar.gz" -C "$(dirname "$MUSL_SRC")"
  rm -rf "$tmp"
fi

BUILD="$OUT_DIR/musl-build"
if [[ ! -f "$INSTALL_LIBC" ]]; then
  rm -rf "$BUILD"
fi
if [[ ! -f "$BUILD/config.mak" ]]; then
  rm -rf "$BUILD"
  mkdir -p "$BUILD" "$OUT_DIR"
  echo "[elf-musl] configure musl in $BUILD (CC=$CC) ..."
  if ! (
    cd "$BUILD"
    "$MUSL_SRC/configure" \
      --prefix="$OUT_DIR/prefix" \
      --disable-shared \
      CC="$CC" \
      AR="$AR" \
      RANLIB="${CROSS}ranlib" \
      CROSS_COMPILE="$CROSS" \
      CFLAGS="-O2 -fno-stack-protector -D__linux__"
  ); then
    echo "[elf-musl] ERROR: musl configure failed (wrong x86_64-elf-gcc/as?)" >&2
    exit 1
  fi
fi

echo "[elf-musl] building lib/libc.a (and libm) ..."
(
  cd "$BUILD"
  make -j"$(nproc 2>/dev/null || echo 4)" lib/libc.a lib/libm.a
  make install
)

cp -f "$BUILD/lib/libc.a" "$INSTALL_LIBC"
cp -f "$BUILD/lib/libm.a" "$INSTALL_LIBM"

# GCC --with-sysroot expects FHS layout (usr/include), musl uses prefix/include.
PREFIX_DIR="$OUT_DIR/prefix"
mkdir -p "$PREFIX_DIR/usr"
[[ -e "$PREFIX_DIR/usr/include" || ! -d "$PREFIX_DIR/include" ]] || ln -sfn ../include "$PREFIX_DIR/usr/include"
[[ -e "$PREFIX_DIR/usr/lib" || ! -d "$PREFIX_DIR/lib" ]] || ln -sfn ../lib "$PREFIX_DIR/usr/lib"

echo "[elf-musl] OK: $INSTALL_LIBC ($(stat -c%s "$INSTALL_LIBC" 2>/dev/null || wc -c <"$INSTALL_LIBC") bytes)"
echo "[elf-musl] OK: $INSTALL_LIBM ($(stat -c%s "$INSTALL_LIBM" 2>/dev/null || wc -c <"$INSTALL_LIBM") bytes)"
