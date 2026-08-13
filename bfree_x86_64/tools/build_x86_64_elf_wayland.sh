#!/usr/bin/env bash
# Cross-build libwayland-client for x86_64-elf (guest QtWayland dependency).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/resolve_elf_musl_paths.sh
source "$ROOT/tools/resolve_elf_musl_paths.sh"
export_elf_musl_paths "$ROOT"
ensure_x86_64_elf_toolchain_path "$ROOT"

PREFIX="$(resolve_elf_out_prefix BFREE_ELF_WAYLAND_DIR x86_64-elf-wayland "$ROOT")"
WAYLAND_VER="${BFREE_WAYLAND_VERSION:-1.23.1}"
SRC="${BFREE_WAYLAND_SRC:-$HOME/src/wayland-${WAYLAND_VER}}"

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

SCANNER_BIN="$(command -v wayland-scanner)"
NATIVE_FILE=""
PC_DIR=""
PKG_CONFIG_WRAPPER=""
if ! env -u PKG_CONFIG_PATH pkg-config --exists "wayland-scanner = ${WAYLAND_VER}" 2>/dev/null; then
  # Meson clears PKG_CONFIG_PATH for native deps during cross builds.
  PC_DIR="$(mktemp -d)"
  cat > "$PC_DIR/wayland-scanner.pc" <<EOF
prefix=$(dirname "$(dirname "$SCANNER_BIN")")
bindir=\${prefix}/bin
wayland_scanner=${SCANNER_BIN}

Name: Wayland Scanner
Description: Wayland scanner
Version: ${WAYLAND_VER}
EOF
  PKG_CONFIG_WRAPPER="$(mktemp)"
  cat > "$PKG_CONFIG_WRAPPER" <<EOF
#!/usr/bin/env bash
export PKG_CONFIG_PATH="${PC_DIR}\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
exec pkg-config "\$@"
EOF
  chmod +x "$PKG_CONFIG_WRAPPER"
  NATIVE_FILE="$(mktemp)"
  cat > "$NATIVE_FILE" <<EOF
[binaries]
pkg-config = '${PKG_CONFIG_WRAPPER}'
EOF
fi

BD="$PREFIX/build"
rm -rf "$BD"
mkdir -p "$PREFIX"

MUSL_SYSROOT="$BFREE_ELF_MUSL_SYSROOT"
if [[ ! -f "$MUSL_SYSROOT/include/stdio.h" ]]; then
  echo "[elf-wayland] musl sysroot missing at $MUSL_SYSROOT — building ..."
  bash "$ROOT/tools/build_x86_64_elf_libm.sh"
  export_elf_musl_paths "$ROOT"
  MUSL_SYSROOT="$BFREE_ELF_MUSL_SYSROOT"
fi
if [[ ! -f "$MUSL_SYSROOT/include/linux/fs.h" ]]; then
  bash "$ROOT/tools/install_musl_kernel_uapi.sh" || true
fi
export BFREE_ELF_MUSL_SYSROOT="$MUSL_SYSROOT"
export BFREE_ELF_CC="${BFREE_ELF_CC:-x86_64-elf-gcc}"
export BFREE_ELF_CXX="${BFREE_ELF_CXX:-x86_64-elf-g++}"

LIBFFI_PREFIX="$(resolve_elf_out_prefix BFREE_ELF_LIBFFI_DIR x86_64-elf-libffi "$ROOT")"
if [[ ! -f "$LIBFFI_PREFIX/lib/pkgconfig/libffi.pc" ]]; then
  echo "[elf-wayland] cross libffi missing — building ..."
  bash "$ROOT/tools/build_x86_64_elf_libffi.sh"
fi
export PKG_CONFIG_PATH="$LIBFFI_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

CROSS_STUBS="$(cd "$ROOT/tools/cross-stubs" && pwd)"
CC="${CC:-x86_64-elf-gcc}"
AR="${AR:-x86_64-elf-ar}"
GCC_WRAP="$ROOT/tools/x86_64-elf-gcc-meson-wrap.sh"
GXX_WRAP="$ROOT/tools/x86_64-elf-gxx-meson-wrap.sh"

# Static archive satisfies meson cc.has_function('clock_gettime') link test.
"$CC" -c -o "$CROSS_STUBS/clock_gettime_stub.o" \
  "$CROSS_STUBS/clock_gettime_stub.c" -I"$CROSS_STUBS"
"$AR" rcs "$CROSS_STUBS/libcrossstub.a" "$CROSS_STUBS/clock_gettime_stub.o"
# Meson cc.find_library('rt') fallback when link-args stub is not enough.
"$AR" rcs "$CROSS_STUBS/librt.a" "$CROSS_STUBS/clock_gettime_stub.o"

CROSS_FILE="$(mktemp)"
sed -e "s|@BFREE_CROSS_STUBS@|${CROSS_STUBS}|g" \
    -e "s|@BFREE_GCC_WRAP@|${GCC_WRAP}|g" \
    -e "s|@BFREE_GXX_WRAP@|${GXX_WRAP}|g" \
  "$ROOT/tools/meson-cross-x86_64-elf.txt" > "$CROSS_FILE"
cleanup() {
  rm -f "$CROSS_FILE"
  [[ -n "$NATIVE_FILE" ]] && rm -f "$NATIVE_FILE"
  [[ -n "$PKG_CONFIG_WRAPPER" ]] && rm -f "$PKG_CONFIG_WRAPPER"
  [[ -n "$PC_DIR" ]] && rm -rf "$PC_DIR"
}
trap cleanup EXIT

MESON_NATIVE=()
if [[ -n "$NATIVE_FILE" ]]; then
  MESON_NATIVE=(--native-file "$NATIVE_FILE")
fi

echo "[elf-wayland] meson cross build -> $PREFIX (stubs=$CROSS_STUBS)"
meson setup "$BD" "$SRC" \
  --cross-file "$CROSS_FILE" \
  "${MESON_NATIVE[@]}" \
  --prefix="$PREFIX" \
  --default-library=static \
  -Ddocumentation=false \
  -Dtests=false \
  -Dlibraries=true \
  -Dscanner=false

ninja -C "$BD"
ninja -C "$BD" install

if [[ ! -f "$PREFIX/lib/pkgconfig/wayland-client.pc" ]]; then
  echo "[elf-wayland] ERROR: install missing wayland-client.pc" >&2
  exit 1
fi

# CMake package configs for guest QtWayland (FindWayland + FindWaylandScanner bypass).
bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$SCANNER_BIN"

echo "[elf-wayland] OK: $PREFIX/lib/pkgconfig/wayland-client.pc"
