#!/usr/bin/env bash
# Relink desktop.elf for D3 Wayland vfork client (stub compositor path).
# Requires maintainer guest Qt prefix + desktop_qt object tree.
# Does not overwrite daily bfree.iso or guest_link_compat.o used by daily desk.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/userland/desktop_qt"

export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export BFREE_GUEST_COMPAT="$ROOT/tools/guest_link_compat.cpp"

echo "[d3-desktop] compile guest_link_compat.o (D3 marker + wayland QPA when /tmp/bfree-d3-wl)"
rm -f guest_link_compat.o
make -f ../Makefile.bfree guest_link_compat.o 2>/dev/null || {
  echo "WARN: Makefile.bfree guest_link_compat.o failed — compile compat manually:" >&2
  echo "  x86_64-elf-g++ -ffreestanding ... -c -o guest_link_compat.o $ROOT/tools/guest_link_compat.cpp" >&2
}

if [[ ! -f guest_link_compat.o ]]; then
  echo "FAIL: guest_link_compat.o missing" >&2
  exit 1
fi

echo "[d3-desktop] relink desktop.elf (needs guest Qt prefix from maintainer)"
if [[ -f Makefile.guest-elf ]]; then
  rm -f desktop.elf
  make -f Makefile.guest-elf -j"$(nproc)" desktop.elf || {
    echo "FAIL: desktop.elf link failed (set BFREE_QT_GUEST_BUILD_DIR / maintainer paths)" >&2
    exit 1
  }
else
  echo "FAIL: Makefile.guest-elf missing" >&2
  exit 1
fi

strings desktop.elf | grep -F 'build=mmap96' | head -1 || true
echo "D3_DESKTOP_ELF=$ROOT/userland/desktop_qt/desktop.elf"
echo "D3_DESKTOP_BYTES=$(wc -c < desktop.elf)"
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
