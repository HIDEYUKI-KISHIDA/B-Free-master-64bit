#!/usr/bin/env bash
# Install prebuilt x86_64-elf gcc+g++ (lordmilko/i686-elf-tools 13.2.0) for guest desktop.elf.
# Ubuntu Noble has no apt g++-x86-64-elf; /usr/local bare gcc has no C++.
#
#   bash tools/install_x86_64_elf_gpp_prebuilt.sh
#   export PATH="$HOME/x86_64-elf-tools/bin:$PATH"
#   which x86_64-elf-g++

set -euo pipefail

# lordmilko zip packs linux/output/* at archive root → bin/ lib/ share/ (not x86_64-elf-tools/).
INSTALL_DIR="${BFREE_X86_64_ELF_TOOLS:-$HOME/x86_64-elf-toolchain}"
VER=13.2.0
ZIP=x86_64-elf-tools-linux.zip
URL="https://github.com/lordmilko/i686-elf-tools/releases/download/${VER}/${ZIP}"

need() { command -v "$1" >/dev/null || { echo "missing: $1 (sudo apt install $2)" >&2; exit 1; }; }
need wget wget
need unzip unzip

if [[ -x "$INSTALL_DIR/bin/x86_64-elf-g++" ]]; then
  echo "[elf-tools] already installed: $INSTALL_DIR/bin/x86_64-elf-g++"
  "$INSTALL_DIR/bin/x86_64-elf-g++" --version | head -1
  echo "[elf-tools] note: this zip has gcc/g++ only — guest Qt link needs musl libc:"
  echo "  bash tools/build_x86_64_elf_libm.sh"
  exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "[elf-tools] downloading $URL ..."
wget -q --show-progress -O "$TMP/$ZIP" "$URL"
echo "[elf-tools] extracting to $INSTALL_DIR ..."
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
unzip -q "$TMP/$ZIP" -d "$INSTALL_DIR"

test -x "$INSTALL_DIR/bin/x86_64-elf-gcc"
if [[ ! -x "$INSTALL_DIR/bin/x86_64-elf-g++" ]]; then
  echo "[elf-tools] WARN: x86_64-elf-g++ missing in zip; only gcc present." >&2
  echo "  Try: ls $INSTALL_DIR/bin/x86_64-elf-*" >&2
  exit 1
fi
test -x "$INSTALL_DIR/bin/x86_64-elf-g++"
echo "[elf-tools] OK:"
ls -la "$INSTALL_DIR/bin/x86_64-elf-g++" "$INSTALL_DIR/bin/x86_64-elf-gcc"
echo ""
echo "Add to ~/.bashrc or before make guest-elf:"
echo "  export PATH=\"$INSTALL_DIR/bin:\$PATH\""
echo "  export BFREE_ELF_GCC_ROOT=\"$INSTALL_DIR\""
echo ""
echo "Then build libm (not included in lordmilko zip):"
echo "  bash tools/build_x86_64_elf_libm.sh"
