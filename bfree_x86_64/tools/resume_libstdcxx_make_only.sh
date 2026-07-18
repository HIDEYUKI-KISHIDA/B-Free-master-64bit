#!/usr/bin/env bash
# Continue libstdc++ after configure: patch asm rules, then make install.
# Self-fix CRLF when invoked from /mnt/c on WSL.
case "$0" in
  /mnt/*|/dev/fd/*|/proc/self/fd/*)
    if grep -q $'\r' "$0" 2>/dev/null; then
      exec bash <(sed 's/\r$//' "$0") "$@"
    fi
    ;;
esac
set -euo pipefail

ROOT="${BFREE_BFREE_X86_64_ROOT:-/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64}"
LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-/root/bfree-native-build}"
BUILD="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
export BFREE_BFREE_X86_64_ROOT="$ROOT"
export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
export BFREE_ELF_GCC_BUILD_DIR="$BUILD"
export BFREE_X86_64_ELF_TOOLS="${BFREE_X86_64_ELF_TOOLS:-/root/x86_64-elf-toolchain}"
export PATH="$BFREE_X86_64_ELF_TOOLS/bin:$PATH"

run_script() {
  local f="$1"
  shift
  if [[ "$f" == /mnt/* ]] && grep -q $'\r' "$f" 2>/dev/null; then
    bash <(sed 's/\r$//' "$f") "$@"
  else
    bash "$f" "$@"
  fi
}

echo "[resume-make] refresh compiler wrappers"
BFREE_REFRESH_WRAPPERS_ONLY=1 run_script "$ROOT/tools/configure_libstdcxx_manual.sh"

run_script "$ROOT/tools/patch_libstdcxx_makefile_asm.sh"

echo "[resume-make] force gthreads (configure probe often fails with musl wrapper)"
run_script "$ROOT/tools/force_libstdcxx_gthreads.sh"

rm -f "$BUILD/x86_64-elf/libstdc++-v3/src/c++11/"*-lt.s \
      "$BUILD/x86_64-elf/libstdc++-v3/src/c++11/tmp-"*-lt.* \
      "$BUILD/x86_64-elf/libstdc++-v3/src/c++98/"*-lt.s 2>/dev/null || true

cd "$BUILD"
nproc="$(nproc 2>/dev/null || echo 4)"
make -j"$nproc" all-target-libstdc++-v3 2>&1 | tee /tmp/libstdcxx-resume.log
make install-target-libstdc++-v3

echo "[resume-make] libstdc++.a:"
find "$LINUX_ROOT/x86_64-elf-gcc-full" -name 'libstdc++.a' 2>/dev/null | head -3
