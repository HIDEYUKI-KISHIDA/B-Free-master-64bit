#!/usr/bin/env bash
# Build libstdc++ only using existing cross-compiler

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GCC_BUILD_ROOT="${BFREE_LINUX_BUILD_ROOT:-/root/bfree-native-build}"
GCC_SRC="$GCC_BUILD_ROOT/gcc-src"
GCC_BUILD="$GCC_BUILD_ROOT/gcc-build"
TOOLCHAIN_PREFIX="/root/x86_64-elf-toolchain"

echo "=== Building libstdc++ only ==="
echo "  gcc src: $GCC_SRC"
echo "  gcc build: $GCC_BUILD"
echo "  toolchain: $TOOLCHAIN_PREFIX"

# Download GCC if not present
if [ ! -d "$GCC_SRC" ]; then
  echo "Downloading GCC..."
  mkdir -p "$GCC_BUILD_ROOT"
  cd "$GCC_BUILD_ROOT"
  wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
  tar xf gcc-13.2.0.tar.xz
  mv gcc-13.2.0 "$GCC_SRC"
fi

# Download prerequisites
cd "$GCC_SRC"
if [ ! -f "mpfr/configure" ]; then
  ./contrib/download_prerequisites
fi

# Configure GCC for libstdc++ only
mkdir -p "$GCC_BUILD"
cd "$GCC_BUILD"

export PATH="$TOOLCHAIN_PREFIX/bin:$PATH"

# Configure with minimal options - just for libstdc++
"$GCC_SRC/configure" \
  --target=x86_64-elf \
  --prefix="$TOOLCHAIN_PREFIX" \
  --enable-languages=c++ \
  --disable-multilib \
  --disable-bootstrap \
  --disable-libssp \
  --disable-libquadmath \
  --disable-libgomp \
  --disable-libitm \
  --disable-libatomic \
  --disable-libvtv \
  --disable-libsanitizer \
  --disable-libstdcxx-pch \
  --disable-libstdcxx-verbose \
  --disable-libstdcxx-debug \
  --with-newlib \
  --without-headers

# Build only libstdc++
echo "Building libstdc++..."
make -j2 all-target-libstdc++-v3

# Install libstdc++
echo "Installing libstdc++..."
make install-target-libstdc++-v3

echo ""
echo "=== libstdc++ installed ==="
ls -la "$TOOLCHAIN_PREFIX/x86_64-elf/lib"/libstdc++*.a || true
