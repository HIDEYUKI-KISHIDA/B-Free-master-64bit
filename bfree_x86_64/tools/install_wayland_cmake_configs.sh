#!/usr/bin/env bash
# Install WaylandConfig.cmake + WaylandScannerConfig.cmake for guest QtWayland cross-build.
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
WAYLAND_VER="${BFREE_WAYLAND_VERSION:-1.23.1}"
SCANNER="${1:-$(command -v wayland-scanner)}"

[[ -n "$SCANNER" && -x "$SCANNER" ]] || {
  echo "[wayland-cmake] wayland-scanner not found" >&2
  exit 1
}
[[ -f "$PREFIX/lib/libwayland-client.a" ]] || {
  echo "[wayland-cmake] missing $PREFIX/lib/libwayland-client.a — run build_x86_64_elf_wayland.sh" >&2
  exit 1
}

missing=()
for lib in client server cursor egl; do
  [[ -f "$PREFIX/lib/libwayland-${lib}.a" ]] || missing+=("libwayland-${lib}.a")
  [[ -f "$PREFIX/include/wayland-${lib}.h" ]] || missing+=("wayland-${lib}.h")
done
if ((${#missing[@]})); then
  echo "[wayland-cmake] incomplete cross libwayland under $PREFIX:" >&2
  printf '  %s\n' "${missing[@]}" >&2
  echo "  bash $ROOT/tools/build_x86_64_elf_wayland.sh" >&2
  exit 1
fi

mkdir -p "$PREFIX/lib/cmake/Wayland" "$PREFIX/lib/cmake/WaylandScanner"
sed -e "s|@PREFIX@|${PREFIX}|g" \
    -e "s|@VERSION@|${WAYLAND_VER}|g" \
    -e "s|@LIBFFI_PREFIX@|${LIBFFI}|g" \
  "$ROOT/tools/cmake/WaylandConfig.cmake.in" > "$PREFIX/lib/cmake/Wayland/WaylandConfig.cmake"
sed -e "s|@SCANNER@|${SCANNER}|g" \
  "$ROOT/tools/cmake/WaylandScannerConfig.cmake.in" > "$PREFIX/lib/cmake/WaylandScanner/WaylandScannerConfig.cmake"
echo "[wayland-cmake] OK: $PREFIX/lib/cmake/Wayland/WaylandConfig.cmake"
