#!/usr/bin/env bash
# Rebuild kernel (VMM), guest desktop.elf, ISO. Run in WSL from repo root:
#   sed -i 's/\r$//' tools/rebuild_vmm_guest_iso.sh tools/bfree_resolve_toolchain.sh
#   bash tools/rebuild_vmm_guest_iso.sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# shellcheck source=bfree_resolve_toolchain.sh
. "$ROOT/tools/bfree_resolve_toolchain.sh"
bfree_apply_toolchain_env || exit 1

export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
export BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
export BFREE_ELF_GCC_ROOT BFREE_ELF_BINUTILS_DIR BFREE_ELF_LIBSTDCXX_PATH PATH

GCC_ROOT="$BFREE_ELF_GCC_ROOT"
BINUTILS="$BFREE_ELF_BINUTILS_DIR"

echo "=== 1/3 kernel ==="
make -C kernel \
  BFREE_ELF_GCC_ROOT="$GCC_ROOT" \
  BFREE_ELF_BINUTILS_DIR="$BINUTILS" \
  clean all

echo "=== 2/3 guest desktop.elf ==="
make -C userland/desktop_qt -f Makefile.bfree guest-elf \
  BFREE_QT_GUEST_BUILD_DIR="$BFREE_QT_GUEST_BUILD_DIR" \
  BFREE_QT_BUILD_DIR="$BFREE_QT_BUILD_DIR" \
  BFREE_ELF_GCC_ROOT="$GCC_ROOT" \
  BFREE_ELF_BINUTILS_DIR="$BINUTILS"

echo "=== readelf (expect VirtAddr ~ 0x02800000) ==="
"$BINUTILS/x86_64-elf-readelf" -l userland/desktop_qt/desktop.elf | grep -A2 LOAD | head -6

echo "=== 3/3 ISO ==="
export BFREE_QT_GUEST_LINKED=1
bash build.sh

echo "Done. Run: bash build_and_run.sh"
