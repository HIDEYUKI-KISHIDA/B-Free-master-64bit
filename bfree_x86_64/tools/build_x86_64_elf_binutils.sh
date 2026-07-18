#!/usr/bin/env bash
# Build native ELF x86_64-elf binutils on WSL/Linux (apt package often missing).
# Installs to: $BFREE_LINUX_BUILD_ROOT/x86_64-elf-binutils (ext4; not /mnt/c).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
PREFIX="${BFREE_ELF_BINUTILS_PREFIX:-$LINUX_ROOT/x86_64-elf-binutils}"
VER="${BFREE_BINUTILS_VERSION:-2.41}"
SRCDIR="$LINUX_ROOT/src"
TARBALL="binutils-$VER.tar.xz"
URL="https://ftp.gnu.org/gnu/binutils/$TARBALL"

export BFREE_BFREE_X86_64_ROOT="$ROOT"
# shellcheck source=bfree_elf_binutils.sh
. "$ROOT/tools/bfree_elf_binutils.sh"

if [[ -x "$PREFIX/bin/x86_64-elf-ld" ]] && bfree_binutils_tool_ok "$PREFIX/bin/x86_64-elf-ld"; then
  echo "[elf-binutils] already installed: $PREFIX/bin/x86_64-elf-ld"
  echo "  export BFREE_ELF_BINUTILS_DIR=$PREFIX/bin"
  exit 0
fi

for cmd in gcc g++ make wget tar file; do
  command -v "$cmd" >/dev/null || {
    echo "[elf-binutils] missing $cmd — install build deps:" >&2
    echo "  sudo apt update && sudo apt install -y build-essential flex bison texinfo wget file" >&2
    exit 1
  }
done

mkdir -p "$SRCDIR" "$PREFIX"
cd "$SRCDIR"
if [[ ! -f "$TARBALL" ]]; then
  echo "[elf-binutils] downloading $URL"
  wget -O "$TARBALL" "$URL"
fi
if [[ ! -d "binutils-$VER" ]]; then
  tar -xf "$TARBALL"
fi

BUILDDIR="$LINUX_ROOT/binutils-build-$VER"
rm -rf "$BUILDDIR"
mkdir -p "$BUILDDIR"
cd "$BUILDDIR"

echo "[elf-binutils] configure -> $PREFIX"
"$SRCDIR/binutils-$VER/configure" \
  --prefix="$PREFIX" \
  --target=x86_64-elf \
  --disable-multilib \
  --disable-werror \
  --disable-nls

make -j"$(nproc)"
make install

LD="$PREFIX/bin/x86_64-elf-ld"
if ! bfree_binutils_tool_ok "$LD"; then
  echo "[elf-binutils] ERROR: install succeeded but $LD is not ELF:" >&2
  file -b -L "$LD" >&2 || true
  exit 1
fi

echo "[elf-binutils] OK: $LD ($(file -b -L "$LD"))"
echo "  export BFREE_ELF_BINUTILS_DIR=$PREFIX/bin"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
