#!/usr/bin/env bash
# Cross-build libwayland-client for x86_64-elf (guest QtWayland dependency).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
WAYLAND_VER="${BFREE_WAYLAND_VERSION:-1.23.1}"
SRC="${BFREE_WAYLAND_SRC:-$HOME/src/wayland-${WAYLAND_VER}}"

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh") || true

need() { command -v "$1" >/dev/null 2>&1 || { echo "[elf-wayland] missing: $1" >&2; return 1; }; }

if ! need meson || ! need ninja || ! need x86_64-elf-gcc || ! need pkg-config; then
  echo "[elf-wayland] install build tools (fix dpkg first if apt fails):" >&2
  echo "  sudo dpkg --configure -a" >&2
  echo "  sudo apt install -y wayland-protocols libwayland-dev meson ninja-build pkg-config" >&2
  echo "  # or: pip install --user meson ninja" >&2
  exit 1
fi

if [[ -f "$PREFIX/lib/pkgconfig/wayland-client.pc" ]]; then
  echo "[elf-wayland] already built: $PREFIX"
  exit 0
fi

if [[ ! -f "$SRC/meson.build" ]]; then
  mkdir -p "$(dirname "$SRC")"
  tmp="$(mktemp -d)"
  echo "[elf-wayland] fetching wayland $WAYLAND_VER ..."
  curl -fsSL "https://gitlab.freedesktop.org/wayland/wayland/-/releases/${WAYLAND_VER}/downloads/wayland-${WAYLAND_VER}.tar.xz" \
    | tar -xJ -C "$tmp"
  mv "$tmp/wayland-${WAYLAND_VER}" "$SRC"
  rm -rf "$tmp"
fi

# Host tools for protocol codegen (wayland-scanner).
if ! command -v wayland-scanner >/dev/null 2>&1; then
  echo "[elf-wayland] install host wayland-scanner:" >&2
  echo "  sudo apt install wayland-protocols libwayland-dev" >&2
  exit 1
fi

BD="$PREFIX/build"
rm -rf "$BD"
mkdir -p "$PREFIX"

echo "[elf-wayland] meson cross build -> $PREFIX"
meson setup "$BD" "$SRC" \
  --cross-file "$ROOT/tools/meson-cross-x86_64-elf.txt" \
  --prefix="$PREFIX" \
  --default-library=static \
  -Ddocumentation=false \
  -Dtests=false \
  -Dlibraries=true \
  -Dscanner=true

ninja -C "$BD"
ninja -C "$BD" install

if [[ ! -f "$PREFIX/lib/pkgconfig/wayland-client.pc" ]]; then
  echo "[elf-wayland] ERROR: install missing wayland-client.pc" >&2
  exit 1
fi
echo "[elf-wayland] OK: $PREFIX/lib/pkgconfig/wayland-client.pc"
