#!/usr/bin/env bash
# Ensure hosted libstdc++ sees pthread/gthreads (musl wrapper breaks configure probe).
set -euo pipefail

LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
BUILD="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
LIBDIR="$BUILD/x86_64-elf/libstdc++-v3"
PREFIX="${BFREE_ELF_GCC_PREFIX:-$LINUX_ROOT/x86_64-elf-gcc-full}"
VER="${BFREE_GCC_VERSION:-13.2.0}"

patch_gthreads() {
  local f="$1"
  [[ -f "$f" ]] || return 0
  grep -q '_GLIBCXX_HAS_GTHREADS' "$f" || return 0
  sed -i 's|^#undef _GLIBCXX_HAS_GTHREADS$|/* #undef _GLIBCXX_HAS_GTHREADS */|' "$f"
  sed -i 's|/\* #undef _GLIBCXX_HAS_GTHREADS \*/|#define _GLIBCXX_HAS_GTHREADS 1|' "$f"
  if ! grep -q '^#define _GLIBCXX_HAS_GTHREADS' "$f"; then
    echo '#define _GLIBCXX_HAS_GTHREADS 1' >>"$f"
  fi
  echo "[gthreads] patched $f"
}

[[ -d "$LIBDIR" ]] || {
  echo "[gthreads] ERROR: missing $LIBDIR — run tools/configure_libstdcxx_manual.sh first" >&2
  exit 1
}

patch_gthreads "$LIBDIR/config.h"
shopt -s nullglob
for f in "$LIBDIR"/include/*/bits/c++config.h; do
  patch_gthreads "$f"
done
for f in "$PREFIX/include/c++/$VER"/x86_64-pc-elf/bits/c++config.h "$PREFIX/include/c++/$VER"/bits/c++config.h; do
  patch_gthreads "$f"
done

GT="$LIBDIR/include/x86_64-pc-elf/bits/gthr-default.h"
if [[ -L "$GT" || -f "$GT" ]]; then
  echo "[gthreads] gthr-default.h -> $(readlink -f "$GT" 2>/dev/null || readlink "$GT" 2>/dev/null || cat "$GT" | head -1)"
else
  echo "[gthreads] WARN: missing $GT (re-run configure_libstdcxx_manual.sh)" >&2
fi

if grep -rq '^#define _GLIBCXX_HAS_GTHREADS' "$LIBDIR/include" 2>/dev/null; then
  echo "[gthreads] OK — rebuild libstdc++:"
  echo "  cd $BUILD && make -j\$(nproc) all-target-libstdc++-v3 install-target-libstdc++-v3"
else
  echo "[gthreads] ERROR: _GLIBCXX_HAS_GTHREADS still missing under $LIBDIR/include" >&2
  exit 1
fi
