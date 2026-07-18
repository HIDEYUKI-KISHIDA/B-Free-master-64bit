#!/usr/bin/env bash
# Resume libstdc++ build after fix_gcc_as_ld_wrappers fix (run as root).
#
#   cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
#   bash tools/resume_libstdcxx_build.sh

if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
set -eu

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[resume] ERROR: run as root (lordmilko toolchain and build tree are under /root)." >&2
  echo "  sudo -i" >&2
  echo "  export PATH=/root/x86_64-elf-toolchain/bin:/usr/bin:/bin" >&2
  echo "  cd /mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64" >&2
  echo "  bash tools/resume_libstdcxx_build.sh" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export BFREE_ROOT="$ROOT"
export BFREE_BFREE_X86_64_ROOT="$ROOT"
export BFREE_LINUX_BUILD_ROOT="${BFREE_LINUX_BUILD_ROOT:-/root/bfree-native-build}"
export BFREE_ELF_MUSL_SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-/root/out/x86_64-elf-libm/prefix}"
export BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-/root/out/x86_64-elf-libm}"
export BFREE_X86_64_ELF_TOOLS="${BFREE_X86_64_ELF_TOOLS:-/root/x86_64-elf-toolchain}"
export PATH="$BFREE_X86_64_ELF_TOOLS/bin:$PATH"
export BFREE_BUILD_JOBS="${BFREE_BUILD_JOBS:-4}"

echo "[resume] ROOT=$ROOT"
echo "[resume] libstdc++ build (log: $BFREE_LINUX_BUILD_ROOT/gcc-build/build.log)"

LIBSTDCXX_DIR="$BFREE_LINUX_BUILD_ROOT/gcc-build/x86_64-elf/libstdc++-v3"
if [[ -f "$LIBSTDCXX_DIR/config.status" ]] \
   && ! grep -q 'enable-clocale=generic' "$LIBSTDCXX_DIR/config.status" 2>/dev/null; then
  echo "[resume] stale libstdc++ configure (glibc ctype) — removing $LIBSTDCXX_DIR"
  rm -rf "$LIBSTDCXX_DIR"
fi

bash "$ROOT/tools/build_x86_64_elf_libstdcxx.sh"

echo "[resume] install libstdc++ into lordmilko toolchain"
GCC_FULL="${BFREE_ELF_GCC_ROOT:-$BFREE_LINUX_BUILD_ROOT/x86_64-elf-gcc-full}"
export BFREE_ELF_LIBSTDCXX_PATH="${BFREE_ELF_LIBSTDCXX_PATH:-$GCC_FULL/lib/libstdc++.a}"
export BFREE_ELF_LIBSUPCXX_PATH="${BFREE_ELF_LIBSUPCXX_PATH:-$GCC_FULL/lib/libsupc++.a}"
bash "$ROOT/tools/install_x86_64_elf_libstdcxx.sh"

LIB="$(x86_64-elf-g++ -print-file-name=libstdc++.a)"
echo "[resume] libstdc++.a => $LIB"
if [[ "$LIB" == libstdc++.a || ! -f "$LIB" ]]; then
  echo "[resume] FAIL: libstdc++ not installed" >&2
  exit 1
fi

echo "[resume] OK — next: bash tools/restore_wsl_guest_env.sh --yes"
