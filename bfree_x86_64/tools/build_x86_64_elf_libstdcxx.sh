#!/usr/bin/env bash
# Build x86_64-elf GCC 13.2 + libstdc++.a using musl sysroot (self-hosted runtime).
# Output: out/x86_64-elf-gcc-full/  (use PATH or BFREE_ELF_GCC_ROOT)
#
#   bash tools/build_x86_64_elf_libm.sh      # first
#   bash tools/run_lf.sh tools/build_x86_64_elf_libstdcxx.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GCC_VER="${BFREE_GCC_VERSION:-13.2.0}"

# GCC source + build on native Linux fs only (/mnt/c drvfs breaks tar/make).
if [[ -n "${BFREE_LINUX_BUILD_ROOT:-}" ]]; then
  LINUX_ROOT="$BFREE_LINUX_BUILD_ROOT"
elif [[ "$ROOT" == /mnt/* ]]; then
  LINUX_ROOT="${HOME}/bfree-native-build"
else
  LINUX_ROOT="$ROOT/out"
fi

SRC_ROOT="${BFREE_GCC_SRC_ROOT:-$LINUX_ROOT/gcc-src}"
GCC_SRC="$SRC_ROOT/gcc-${GCC_VER}"
PREFIX="${BFREE_ELF_GCC_PREFIX:-$LINUX_ROOT/x86_64-elf-gcc-full}"
SYSROOT="${BFREE_ELF_MUSL_SYSROOT:-$ROOT/out/x86_64-elf-libm/prefix}"
BUILD_DIR="${BFREE_ELF_GCC_BUILD_DIR:-$LINUX_ROOT/gcc-build}"
LOG="$BUILD_DIR/build.log"
JOBS="${BFREE_BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "[elf-gcc++] repo=$ROOT"
echo "[elf-gcc++] native build root=$LINUX_ROOT (override: BFREE_LINUX_BUILD_ROOT)"

if compgen -G "$PREFIX/lib/gcc/x86_64-elf/"*/libstdc++.a >/dev/null 2>&1 \
   || [[ -f "$PREFIX/lib/libstdc++.a" ]]; then
  echo "[elf-gcc++] already built:"
  ls -la "$PREFIX"/lib/gcc/x86_64-elf/*/libstdc++.a 2>/dev/null \
    || ls -la "$PREFIX/lib/libstdc++.a"
  echo "  PATH=$PREFIX/bin:\$PATH"
  exit 0
fi

need() { command -v "$1" >/dev/null || { echo "missing: $1 — sudo apt install $2" >&2; exit 1; }; }
need wget wget
need make make
need g++ g++

# Cross binutils (as/ld/ar) required while building target-libgcc; do not use PATH=/usr/bin:/bin only.
BINUTILS_DIR=""
prepend_binutils_path() {
  local d lordmilko="${BFREE_X86_64_ELF_TOOLS:-/root/x86_64-elf-toolchain}/bin"
  for d in \
    "${BFREE_ELF_BINUTILS_DIR:-}" \
    "$lordmilko" \
    "${HOME}/bin" \
    "${HOME}/x86_64-elf-toolchain/bin" \
    /usr/local/x86_64-elf/bin \
    /root/x86_64-elf-toolchain/bin \
    /root/bin; do
    [[ -n "$d" && -x "$d/x86_64-elf-as" && -x "$d/x86_64-elf-ld" ]] || continue
    BINUTILS_DIR="$d"
    PATH="$d:$PATH"
    echo "[elf-gcc++] binutils: $d (x86_64-elf-as)"
    export PATH
    return 0
  done
  echo "[elf-gcc++] ERROR: x86_64-elf-as / x86_64-elf-ld not found." >&2
  if [[ -d /root/x86_64-elf-toolchain/bin ]] && ! ls "/root/x86_64-elf-toolchain/bin/x86_64-elf-as" &>/dev/null; then
    echo "  Lordmilko toolchain is under /root (not readable as $(id -un)); run: sudo -i" >&2
  else
    echo "  Install lordmilko toolchain or: export BFREE_ELF_BINUTILS_DIR=/path/to/bin" >&2
  fi
  exit 1
}
prepend_binutils_path
need x86_64-elf-as binutils
need x86_64-elf-ld binutils

fix_musl_link_after_gcc() {
  local s="$ROOT/tools/fix_gcc_musl_link.sh" d="/tmp/bfree-fix_gcc_musl_link.$$" sr="$1"
  [[ -f "$s" ]] || return 0
  tr -d '\r' <"$s" >"$d"
  chmod +x "$d"
  bash "$d" "$sr"
  rm -f "$d"
}

stage_binutils_for_target_libgcc() {
  local dst="$PREFIX/x86_64-elf/bin" t
  mkdir -p "$dst"
  for t in as ld ar nm ranlib strip objcopy objdump readelf; do
    [[ -x "$BINUTILS_DIR/x86_64-elf-$t" ]] || continue
    install -m 755 "$BINUTILS_DIR/x86_64-elf-$t" "$dst/x86_64-elf-$t"
  done
  echo "[elf-gcc++] staged binutils -> $dst (for xgcc -B.../x86_64-elf/bin/)"
}

if [[ ! -f "$SYSROOT/include/stdio.h" ]]; then
  echo "[elf-gcc++] musl sysroot missing: $SYSROOT/include/stdio.h" >&2
  echo "  Run: bash tools/run_lf.sh tools/build_x86_64_elf_libm.sh" >&2
  exit 1
fi

# musl installs include/ and lib/ at prefix root; GCC fixincludes expects usr/include.
prepare_musl_sysroot_for_gcc() {
  local root="$1"
  mkdir -p "$root/usr"
  if [[ ! -e "$root/usr/include" && -d "$root/include" ]]; then
    ln -sfn ../include "$root/usr/include"
    echo "[elf-gcc++] sysroot: $root/usr/include -> ../include"
  fi
  if [[ ! -e "$root/usr/lib" && -d "$root/lib" ]]; then
    ln -sfn ../lib "$root/usr/lib"
    echo "[elf-gcc++] sysroot: $root/usr/lib -> ../lib"
  fi
  [[ -d "$root/usr/include" ]] || {
    echo "[elf-gcc++] ERROR: $root/usr/include missing (need musl headers under $root/include)" >&2
    exit 1
  }
}
prepare_musl_sysroot_for_gcc "$SYSROOT"

mkdir -p "$SRC_ROOT" "$BUILD_DIR" "$(dirname "$PREFIX")"
if [[ ! -f "$GCC_SRC/configure" ]]; then
  if [[ -d "$GCC_SRC" ]]; then
    echo "[elf-gcc++] removing incomplete $GCC_SRC ..."
    rm -rf "$GCC_SRC"
  fi
  echo "[elf-gcc++] downloading gcc-$GCC_VER ..."
  tmp=$(mktemp -d)
  wget -q --show-progress -O "$tmp/gcc.tar.xz" \
    "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"
  echo "[elf-gcc++] extracting to $SRC_ROOT (native fs; not /mnt/c) ..."
  tar --no-same-owner --no-same-permissions -xf "$tmp/gcc.tar.xz" -C "$SRC_ROOT"
  rm -rf "$tmp"
  echo "[elf-gcc++] gmp/mpfr/mpc ..."
  (cd "$GCC_SRC" && ./contrib/download_prerequisites)
fi

if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
  echo "[elf-gcc++] configure in $BUILD_DIR (prefix=$PREFIX sysroot=$SYSROOT) ..."
  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"
  (
    cd "$BUILD_DIR"
    "$GCC_SRC/configure" \
      --srcdir="$GCC_SRC" \
      --target=x86_64-elf \
      --prefix="$PREFIX" \
      --with-as="$BINUTILS_DIR/x86_64-elf-as" \
      --with-ld="$BINUTILS_DIR/x86_64-elf-ld" \
      --enable-languages=c,c++ \
      --disable-multilib \
      --disable-nls \
      --enable-static \
      --disable-shared \
      --with-sysroot="$SYSROOT" \
      --with-newlib \
      --with-native-system-header-dir=/usr/include \
      --disable-bootstrap
  ) 2>&1 | tee "$BUILD_DIR/configure.log"
fi

if ! grep -q '^all-gcc:' "$BUILD_DIR/Makefile" 2>/dev/null; then
  echo "[elf-gcc++] ERROR: $BUILD_DIR/Makefile is not a GCC tree (configure in wrong directory?)" >&2
  echo "  Fix: rm -rf $BUILD_DIR && re-run this script" >&2
  exit 1
fi

echo "[elf-gcc++] building (jobs=$JOBS) — log: $LOG"
echo "[elf-gcc++] order: all-gcc → all-target-libgcc → all-target-libstdc++-v3 → install-*"
{
  echo "=== $(date -u) ==="
  cd "$BUILD_DIR"
  make -j"$JOBS" all-gcc
  stage_binutils_for_target_libgcc
  export BFREE_BFREE_X86_64_ROOT="$ROOT"
  export BFREE_ROOT="$ROOT"
  bash "$ROOT/tools/fix_gcc_as_ld_wrappers.sh"
  fix_musl_link_after_gcc "$SYSROOT"
  rm -rf x86_64-elf/libgcc
  make -j"$JOBS" all-target-libgcc
  if ! compgen -G "x86_64-elf/libgcc/libgcc.a" >/dev/null 2>&1 \
     && ! compgen -G "x86_64-elf/libgcc/libgcc_eh.a" >/dev/null 2>&1; then
    echo "[elf-gcc++] ERROR: all-target-libgcc did not produce libgcc.a" >&2
    echo "  See x86_64-elf/libgcc/config.log and build.log" >&2
    exit 1
  fi
  # make configure-target-libstdc++-v3 picks glibc ctype (_U/_L) and fails on musl.
  # configure_libstdcxx_manual.sh uses --enable-clocale=generic + musl wrappers.
  LIBSTDCXX_DIR="$BUILD_DIR/x86_64-elf/libstdc++-v3"
  if [[ -f "$LIBSTDCXX_DIR/config.status" ]] \
     && ! grep -q 'enable-clocale=generic' "$LIBSTDCXX_DIR/config.status" 2>/dev/null; then
    echo "[elf-gcc++] removing libstdc++ tree (missing --enable-clocale=generic; ctype_base/_U fails on musl)"
    rm -rf "$LIBSTDCXX_DIR"
  fi
  export BFREE_LINUX_BUILD_ROOT="$LINUX_ROOT"
  export BFREE_ELF_GCC_BUILD_DIR="$BUILD_DIR"
  export BFREE_ELF_GCC_PREFIX="$PREFIX"
  export BFREE_ELF_MUSL_SYSROOT="$SYSROOT"
  export BFREE_ELF_BINUTILS_DIR="$BINUTILS_DIR"
  bash "$ROOT/tools/configure_libstdcxx_manual.sh"
  export BFREE_ELF_GCC_BUILD_DIR="$BUILD_DIR"
  bash "$ROOT/tools/patch_libstdcxx_makefile_asm.sh"
  libstdcxx_in_build_tree() {
    compgen -G "x86_64-elf/libstdc++-v3/src/libstdc++.a" >/dev/null 2>&1 \
      || compgen -G "x86_64-elf/libstdc++-v3/src/.libs/libstdc++.a" >/dev/null 2>&1
  }
  if libstdcxx_in_build_tree; then
    echo "[elf-gcc++] libstdc++.a already in build tree; skipping all-target-libstdc++-v3"
    ls -la x86_64-elf/libstdc++-v3/src/libstdc++.a \
      x86_64-elf/libstdc++-v3/src/.libs/libstdc++.a 2>/dev/null || true
  else
    make -j"$JOBS" all-target-libstdc++-v3
    if ! libstdcxx_in_build_tree; then
      echo "[elf-gcc++] ERROR: all-target-libstdc++-v3 did not produce libstdc++.a" >&2
      exit 1
    fi
  fi
  # Do not install-gcc before target libs: prefix/x86_64-elf/{bin,include} confuses libgcc.
  make install-gcc install-target-libgcc install-target-libstdc++-v3
} 2>&1 | tee -a "$LOG"

if ! compgen -G "$PREFIX/lib/gcc/x86_64-elf/"*/libstdc++.a >/dev/null 2>&1 \
   && [[ ! -f "$PREFIX/lib/libstdc++.a" ]]; then
  echo "[elf-gcc++] ERROR: libstdc++.a not found under $PREFIX" >&2
  echo "  See $LOG" >&2
  exit 1
fi

echo "[elf-gcc++] OK:"
ls -la "$PREFIX"/lib/gcc/x86_64-elf/*/libstdc++.a "$PREFIX"/lib/gcc/x86_64-elf/*/libsupc++.a 2>/dev/null || true
echo ""
echo "Add to ~/.bashrc or before make guest-elf:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo "  export BFREE_ELF_GCC_ROOT=\"$PREFIX\""
echo ""
echo "Optional formal bundle:"
echo "  export BFREE_ELF_LIBSTDCXX_PATH=\$(x86_64-elf-g++ -print-file-name=libstdc++.a)"
echo "  bash tools/run_lf.sh tools/stage_x86_64_elf_runtime.sh"
