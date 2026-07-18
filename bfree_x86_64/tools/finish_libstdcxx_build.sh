#!/usr/bin/env bash
# Build + install target libstdc++ (after libgcc is installed).
if [ -z "${BFREE_FIX_CRLF_DONE:-}" ] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash -c "$(tr -d '\r' <"$0")" _ "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export BFREE_LINUX_BUILD_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
export BFREE_ELF_BINUTILS_DIR="${BFREE_ELF_BINUTILS_DIR:-/usr/local/x86_64-elf/bin}"
BUILD="$BFREE_LINUX_BUILD_ROOT/gcc-build"
PREFIX="${BFREE_ELF_GCC_PREFIX:-$BFREE_LINUX_BUILD_ROOT/x86_64-elf-gcc-full}"
JOBS="${BFREE_BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"

export PATH="${BFREE_ELF_BINUTILS_DIR}:${PREFIX}/bin:${HOME}/bin:/usr/bin:/bin"

find_build_libstdcxx() {
  local f
  for f in \
    "$BUILD/x86_64-elf/libstdc++-v3/src/libstdc++.a" \
    "$BUILD/x86_64-elf/libstdc++-v3/src/.libs/libstdc++.a"; do
    [ -f "$f" ] && { echo "$f"; return 0; }
  done
  f=$(find "$BUILD/x86_64-elf/libstdc++-v3" -name 'libstdc++.a' -type f 2>/dev/null | head -1)
  [ -n "$f" ] && { echo "$f"; return 0; }
  return 1
}

find_installed_libstdcxx() {
  local f
  for f in \
    "$PREFIX/lib/libstdc++.a" \
    "$PREFIX/lib/gcc/x86_64-elf/"*/libstdc++.a; do
    [ -f "$f" ] && { echo "$f"; return 0; }
  done
  return 1
}

echo "[finish-libstdc++] NOT using: make configure-target-libstdc++-v3"
bash "$ROOT/tools/configure_libstdcxx_manual.sh"

cd "$BUILD"
echo "[finish-libstdc++] all-target-libstdc++-v3 (jobs=$JOBS) ..."
make -j"$JOBS" all-target-libstdc++-v3

if f=$(find_build_libstdcxx); then
  echo "[finish-libstdc++] build tree: $f"
  ls -la "$f"
else
  echo "[finish-libstdc++] note: libstdc++.a not in src/ yet (normal — created at install)"
fi

echo "[finish-libstdc++] install-target-libstdc++-v3 ..."
make install-target-libstdc++-v3

LIBA_INST=$(find_installed_libstdcxx || true)
[ -n "$LIBA_INST" ] || {
  echo "[finish-libstdc++] ERROR: libstdc++.a not under $PREFIX after install" >&2
  echo "  try: find $BUILD/x86_64-elf/libstdc++-v3 -name 'libstdc++.a'" >&2
  exit 1
}
ls -la "$LIBA_INST" "$PREFIX/lib/gcc/x86_64-elf/"*/libsupc++.a 2>/dev/null || true

# Guest link uses $PREFIX/lib/libstdc++.a (install puts it there; copy libsupc++ beside it).
if [ -f "$PREFIX/lib/libstdc++.a" ]; then
  for sup in "$PREFIX/lib/gcc/x86_64-elf/"*/libsupc++.a "$BUILD/x86_64-elf/libstdc++-v3/src/.libs/libsupc++.a"; do
    [ -f "$sup" ] || continue
    cp -f "$sup" "$PREFIX/lib/libsupc++.a"
    echo "[finish-libstdc++] libsupc++.a -> $PREFIX/lib/libsupc++.a"
    break
  done
fi

bash "$ROOT/tools/check_libstdcxx_threads.sh" "${LIBA_INST:-$PREFIX/lib/libstdc++.a}"
echo ""
echo "export PATH=\"$PREFIX/bin:\$PATH\""
echo "export BFREE_ELF_GCC_ROOT=\"$PREFIX\""
echo "export BFREE_ELF_LIBSTDCXX_PATH=\"$PREFIX/lib/libstdc++.a\""
