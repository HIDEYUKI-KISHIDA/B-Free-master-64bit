#!/usr/bin/env bash
# x86_64-elf GCC was configured with newlib-style crt0.o; musl uses crt1.o + crti.o + crtn.o.
# Do NOT edit gcc/specs (sed breaks the file). Use crt symlinks + LIBRARY_PATH + -nostdlib in wrappers.
set -euo pipefail

BUILD="${BFREE_ELF_GCC_BUILD_DIR:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/gcc-build}"
GCC_DIR="$BUILD/gcc"
PREFIX="${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}"
SYSROOT="${1:?sysroot}"
GCC_VER="${BFREE_GCC_VERSION:-13.2.0}"
GCC_VER_DIR="$PREFIX/lib/gcc/x86_64-elf/$GCC_VER"
LIBGCC_BUILD="$BUILD/x86_64-elf/libgcc"
SPECS="$GCC_DIR/specs"

[[ -d "$GCC_DIR" ]] || { echo "fix_gcc_musl_link: missing $GCC_DIR" >&2; exit 1; }

# Undo broken specs patch from older script versions.
if [[ -f "$SPECS" ]] && grep -q 'bfree-musl-crt' "$SPECS" 2>/dev/null; then
  if [[ -f "$SPECS.bak-bfree-musl" ]]; then
    cp -a "$SPECS.bak-bfree-musl" "$SPECS"
    echo "[fix-gcc-musl] restored $SPECS from .bak-bfree-musl"
  else
    echo "[fix-gcc-musl] WARN: broken specs marker but no backup — run: cd $BUILD && make all-gcc" >&2
  fi
fi

for o in crt1.o crti.o crtn.o; do
  [[ -f "$SYSROOT/lib/$o" ]] || { echo "fix_gcc_musl_link: missing $SYSROOT/lib/$o" >&2; exit 1; }
done

install -m 644 "$SYSROOT/lib/crt1.o" "$GCC_DIR/crt0.o"
ln -sfn "$SYSROOT/lib/crti.o" "$GCC_DIR/crti.o"
ln -sfn "$SYSROOT/lib/crtn.o" "$GCC_DIR/crtn.o"
ln -sfn "$SYSROOT/lib/crt1.o" "$GCC_DIR/crt1.o"

mkdir -p "$PREFIX/x86_64-elf/lib"
install -m 644 "$SYSROOT/lib/crt1.o" "$PREFIX/x86_64-elf/lib/crt0.o"
ln -sfn "$SYSROOT/lib/crti.o" "$PREFIX/x86_64-elf/lib/crti.o"
ln -sfn "$SYSROOT/lib/crtn.o" "$PREFIX/x86_64-elf/lib/crtn.o"

export BFREE_MUSL_LIBRARY_PATH="$SYSROOT/lib:$GCC_VER_DIR:$LIBGCC_BUILD:$GCC_DIR"
echo "[fix-gcc-musl] crt0.o <- musl crt1.o in $GCC_DIR"
echo "[fix-gcc-musl] LIBRARY_PATH=$BFREE_MUSL_LIBRARY_PATH"
echo "[fix-gcc-musl] link via wrapper -nostdlib + crt1/crti/crtn (specs untouched)"
