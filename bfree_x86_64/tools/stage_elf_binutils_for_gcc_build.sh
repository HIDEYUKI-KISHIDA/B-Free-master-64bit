#!/usr/bin/env bash
# Copy lordmilko (or similar) x86_64-elf binutils into the GCC install tree so
# xgcc -B$PREFIX/x86_64-elf/bin/ can assemble when building target-libgcc.
set -euo pipefail

PREFIX="${BFREE_ELF_GCC_PREFIX:-${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}/x86_64-elf-gcc-full}"
DST="$PREFIX/x86_64-elf/bin"

find_binutils_dir() {
  local d
  for d in \
    "${BFREE_ELF_BINUTILS_DIR:-}" \
    "${HOME}/bin" \
    "${HOME}/x86_64-elf-toolchain/bin" \
    /usr/local/x86_64-elf/bin \
    /root/bin; do
    [[ -n "$d" && -x "$d/x86_64-elf-as" && -x "$d/x86_64-elf-ld" ]] || continue
    echo "$d"
    return 0
  done
  return 1
}

SRC="$(find_binutils_dir)" || {
  echo "stage_elf_binutils: x86_64-elf-as/ld not found (set BFREE_ELF_BINUTILS_DIR)" >&2
  exit 1
}

mkdir -p "$DST"
for t in as ld ar nm ranlib strip objcopy objdump readelf; do
  [[ -x "$SRC/x86_64-elf-$t" ]] || continue
  install -m 755 "$SRC/x86_64-elf-$t" "$DST/x86_64-elf-$t"
done

echo "[stage-binutils] $SRC -> $DST"
ls -la "$DST"/x86_64-elf-as "$DST"/x86_64-elf-ld
