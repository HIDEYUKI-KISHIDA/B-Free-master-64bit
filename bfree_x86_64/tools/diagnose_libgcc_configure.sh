#!/usr/bin/env bash
# Print why target-libgcc configure fails ("cannot compute suffix of object files").
set -euo pipefail

BUILD="${BFREE_ELF_GCC_BUILD_DIR:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/gcc-build}"
PREFIX="${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}"
LOG="$BUILD/x86_64-elf/libgcc/config.log"
TGT_BIN="$PREFIX/x86_64-elf/bin"

echo "=== paths ==="
echo "BUILD=$BUILD"
echo "PREFIX=$PREFIX"
echo "TGT_BIN=$TGT_BIN"
echo ""

echo "=== binutils in prefix (xgcc -B looks here first) ==="
ls -la "$TGT_BIN"/x86_64-elf-as "$TGT_BIN"/x86_64-elf-ld 2>&1 || echo "(missing — run tools/stage_elf_binutils_for_gcc_build.sh)"
echo ""

echo "=== PATH binutils ==="
export PATH="${BFREE_ELF_BINUTILS_DIR:-/root/bin}:$HOME/x86_64-elf-toolchain/bin:/usr/local/x86_64-elf/bin:$PATH"
which x86_64-elf-as x86_64-elf-ld 2>&1 || true
echo ""

echo "=== gcc/as wrapper (broken if shell script fails with 'exec: -o: not found') ==="
if [[ -f "$BUILD/gcc/as" ]]; then
  file "$BUILD/gcc/as" || true
  sed -n '108,118p' "$BUILD/gcc/as" 2>/dev/null || true
fi
echo ""

echo "=== xgcc smoke test ==="
cat >/tmp/bfree-conftest.c <<'EOF'
int x;
EOF
set +e
"$BUILD/gcc/xgcc" -B"$BUILD/gcc/" -B"$TGT_BIN/" \
  -c -o /tmp/bfree-conftest.o /tmp/bfree-conftest.c -v 2>&1 | tail -25
echo "exit=$?  object=$(ls -la /tmp/bfree-conftest.o 2>&1)"
set -e
echo ""

echo "=== target include dirs (empty/broken dirs break cc1) ==="
for d in "$PREFIX/x86_64-elf/include" "$PREFIX/x86_64-elf/sys-include"; do
  echo -n "$d: "
  if [[ ! -d "$d" ]]; then echo "MISSING"; continue; fi
  n=$(find "$d" -type f 2>/dev/null | wc -l)
  echo "$n files"
done
echo ""

if [[ -f "$LOG" ]]; then
  echo "=== config.log (last compile attempt) ==="
  grep -n 'conftest\|error:\|fatal error:\|cannot compile\|checking for suffix' "$LOG" | tail -40
  echo ""
  echo "=== config.log (full tail) ==="
  tail -60 "$LOG"
else
  echo "No $LOG — libgcc not configured yet."
fi
