#!/usr/bin/env bash
# Build musl crt files for existing x86_64-elf toolchain

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MUSL_BUILD_ROOT="${BFREE_LINUX_BUILD_ROOT:-/root/bfree-native-build}"
MUSL_SRC="$MUSL_BUILD_ROOT/musl-src"
MUSL_BUILD="$MUSL_BUILD_ROOT/musl-build"
TOOLCHAIN_PREFIX="/root/x86_64-elf-toolchain"

echo "=== Building musl crt files for x86_64-elf ==="
echo "  musl src: $MUSL_SRC"
echo "  musl build: $MUSL_BUILD"
echo "  toolchain: $TOOLCHAIN_PREFIX"

# Download musl if not present
if [ ! -d "$MUSL_SRC" ]; then
  echo "Downloading musl..."
  mkdir -p "$MUSL_BUILD_ROOT"
  cd "$MUSL_BUILD_ROOT"
  wget https://musl.libc.org/releases/musl-1.2.4.tar.gz
  tar xzf musl-1.2.4.tar.gz
  mv musl-1.2.4 "$MUSL_SRC"
fi

# Configure and build musl (crt files only)
mkdir -p "$MUSL_BUILD"
cd "$MUSL_BUILD"

# Configure musl for cross-compilation
CC="$TOOLCHAIN_PREFIX/bin/x86_64-elf-gcc" \
CROSS_COMPILE=x86_64-elf- \
"$MUSL_SRC/configure" \
  --host=x86_64-elf \
  --prefix="$TOOLCHAIN_PREFIX/x86_64-elf" \
  --disable-shared \
  --enable-static

# Build musl library (this will create crt files)
echo "Building musl library..."
cd "$MUSL_BUILD"
export PATH="$TOOLCHAIN_PREFIX/bin:$PATH"
make -j2

# Install crt files
echo "Installing crt files to toolchain..."
mkdir -p "$TOOLCHAIN_PREFIX/x86_64-elf/lib"
cp lib/crt1.o "$TOOLCHAIN_PREFIX/x86_64-elf/lib/crt0.o"
cp lib/crt1.o "$TOOLCHAIN_PREFIX/x86_64-elf/lib/"
cp lib/crti.o "$TOOLCHAIN_PREFIX/x86_64-elf/lib/"
cp lib/crtn.o "$TOOLCHAIN_PREFIX/x86_64-elf/lib/"

# Also copy to GCC lib directory
mkdir -p "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0"
cp lib/crt1.o "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0/crt0.o"
cp lib/crt1.o "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0/"
cp lib/crti.o "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0/"
cp lib/crtn.o "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0/"

echo ""
echo "=== crt files installed ==="
ls -la "$TOOLCHAIN_PREFIX/x86_64-elf/lib"/crt*.o
ls -la "$TOOLCHAIN_PREFIX/lib/gcc/x86_64-elf/13.2.0"/crt*.o || true
