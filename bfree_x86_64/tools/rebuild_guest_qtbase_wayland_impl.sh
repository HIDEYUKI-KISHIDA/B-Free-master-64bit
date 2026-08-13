#!/usr/bin/env bash
# Internal: qtbase-only cross rebuild with Wayland (called by rebuild_guest_qtbase_wayland_only.sh).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI_PREFIX="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
JOBS="${JOBS:-4}"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"

log_phase() { echo "[$(date '+%H:%M:%S')] [guest-qtbase-wayland] $*"; }

resolve_musl_prefix() {
  local base="${BFREE_ELF_LIBM_DIR:-${HOME}/out/x86_64-elf-libm}"
  if [[ -f "$base/prefix/include/stdint.h" ]]; then echo "$base/prefix"
  elif [[ -f "$base/include/stdint.h" ]]; then echo "$base"
  elif [[ -f "$ROOT/out/x86_64-elf-libm/prefix/include/stdint.h" ]]; then echo "$ROOT/out/x86_64-elf-libm/prefix"
  else echo "$base/prefix"; fi
}

libstdcxx_ok() {
  local gxx libstdc libgcc dir
  gxx="$(command -v x86_64-elf-g++)"
  libstdc="$("$gxx" -print-file-name=libstdc++.a 2>/dev/null || true)"
  [[ -n "$libstdc" && -f "$libstdc" ]] && return 0
  libgcc="$("$gxx" -print-file-name=libgcc.a 2>/dev/null || true)"
  dir="$(dirname "$libgcc")"
  [[ -f "$dir/libstdc++.a" ]]
}

host_qt_ok() {
  local p="$1"
  [[ -n "$p" && -d "$p" ]] || return 1
  [[ -x "$p/bin/moc" || -x "$p/libexec/moc" ]] || return 1
  [[ -x "$p/bin/qt-cmake" ]] || return 1
  [[ -r "$p/lib/cmake/Qt6/Qt6Config.cmake" ]] || return 1
}

resolve_host_qt() {
  local p
  for p in "${BFREE_QT_BUILD_DIR:-}" "$HOST_QT" "${HOME}/out/bfree-qt6-static" /root/out/bfree-qt6-static; do
    host_qt_ok "$p" && { echo "$p"; return 0; }
  done
  return 1
}

resolve_elf_cxx_include() {
  local gxx root inc target
  gxx="$(command -v x86_64-elf-g++)"
  root="$(cd "$(dirname "$gxx")/.." && pwd)"
  for inc in \
    "${BFREE_ELF_CXX_INCLUDE:-}" \
    "$(dirname "$(dirname "$("$gxx" -print-file-name=include/c++)" 2>/dev/null || true)")" \
    "$root/lib/gcc/x86_64-elf/"*/include/c++ \
    "$root/include/c++/13.2.0" \
    /root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0; do
    [[ -n "$inc" && -f "$inc/atomic" ]] || continue
    for target in "$inc/x86_64-elf" "$inc/x86_64-pc-elf"; do
      [[ -d "$target" ]] && { echo "$inc|$target"; return 0; }
    done
    echo "$inc|"; return 0
  done
  return 1
}

write_toolchain_cmake() {
  # shellcheck source=tools/guest_qtbase_write_toolchain.sh
  source "$ROOT/tools/guest_qtbase_write_toolchain.sh"
  guest_qtbase_write_toolchain_cmake "$@"
}

need() { command -v "$1" >/dev/null 2>&1 || { echo "[FAIL] missing: $1" >&2; exit 1; }; }
need x86_64-elf-gcc
need x86_64-elf-g++
need cmake
need ninja

libstdcxx_ok || { echo "[FAIL] libstdc++.a missing for x86_64-elf-g++" >&2; exit 1; }
HOST_QT="$(resolve_host_qt || true)"
[[ -n "$HOST_QT" ]] || { echo "[FAIL] host Qt (QT_HOST_PATH) missing" >&2; exit 1; }

if [[ ! -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]]; then
  bash "$ROOT/tools/build_x86_64_elf_wayland.sh"
fi
SCANNER="$(command -v wayland-scanner)"
bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$SCANNER"

MUSL_PREFIX="$(resolve_musl_prefix)"
# install_musl_kernel_uapi.sh defaults BFREE_ELF_LIBM_DIR from $ROOT/out, not /root/out
if [[ "$MUSL_PREFIX" == */prefix ]]; then
  export BFREE_ELF_LIBM_DIR="${MUSL_PREFIX%/prefix}"
else
  export BFREE_ELF_LIBM_DIR="$MUSL_PREFIX"
fi
export BFREE_ELF_MUSL_SYSROOT="$MUSL_PREFIX"
bash "$ROOT/tools/install_musl_kernel_uapi.sh"
bash "$ROOT/tools/patch_qt_guest_linux_fs_h.sh"
LIBGCC_DIR="$(dirname "$(x86_64-elf-g++ -print-file-name=libgcc.a)")"
ELF_ROOT="$(cd "$(dirname "$(command -v x86_64-elf-g++)")/.." && pwd)"
CXX_INC_PAIR="$(resolve_elf_cxx_include)"
ELF_CXX_INC="${CXX_INC_PAIR%%|*}"
ELF_CXX_TARGET="${CXX_INC_PAIR#*|}"

[[ -f "$QT_SRC/qtbase/CMakeLists.txt" ]] || { echo "[FAIL] missing $QT_SRC/qtbase" >&2; exit 1; }

EXTRA_ROOT=";${WAYLAND_PREFIX};${LIBFFI_PREFIX}"
if ! mkdir -p "$PREFIX/build-qtbase" 2>/dev/null; then
  echo "[guest-qtbase-wayland] ERROR: cannot write guest prefix: $PREFIX" >&2
  echo "  export BFREE_QT_GUEST_BUILD_DIR=\$HOME/out/bfree-qt6-guest-static" >&2
  exit 1
fi
rm -rf "$PREFIX/build-qtbase"
mkdir -p "$PREFIX/build-qtbase"
write_toolchain_cmake "$PREFIX/build-qtbase/toolchain.cmake" \
  "$MUSL_PREFIX" "$LIBGCC_DIR" "$ELF_ROOT" "$ELF_CXX_INC" "$ELF_CXX_TARGET" "$EXTRA_ROOT"

log_phase "configure qtbase (Wayland_DIR=$WAYLAND_PREFIX/lib/cmake/Wayland)"
log_phase "this phase is slow (10–30 min); log: $PREFIX/build-qtbase/configure.log"
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PREFIX/build-qtbase/toolchain.cmake" \
  -DQT_HOST_PATH="$HOST_QT" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_qtwebengine=OFF \
  -DQT_BUILD_EXAMPLES=FALSE \
  -DQT_BUILD_TESTS=FALSE \
  -DINPUT_opengl=no \
  -DFEATURE_vulkan=OFF \
  -DFEATURE_dbus=OFF \
  -DINPUT_openssl=no \
  -DFEATURE_network=OFF \
  -DFEATURE_ssl=OFF \
  -DFEATURE_brotli=OFF \
  -DFEATURE_icu=OFF \
  -DFEATURE_glib=OFF \
  -DFEATURE_xcb=OFF \
  -DFEATURE_xkbcommon=OFF \
  -DFEATURE_xcb_xlib=OFF \
  -DFEATURE_system_zlib=OFF \
  -DINPUT_libpng=qt \
  -DINPUT_freetype=qt \
  -DINPUT_harfbuzz=qt \
  -DINPUT_pcre=qt \
  -DFEATURE_fontconfig=OFF \
  -DWayland_DIR="$WAYLAND_PREFIX/lib/cmake/Wayland" \
  -B "$PREFIX/build-qtbase" \
  -S "$QT_SRC/qtbase" \
  2>&1 | tee "$PREFIX/build-qtbase/configure.log"

log_phase "configure finished — checking FEATURE_wayland ..."
if ! grep -qE '^FEATURE_wayland:BOOL=ON$|^QT_FEATURE_wayland:BOOL=ON$' "$PREFIX/build-qtbase/CMakeCache.txt"; then
  log_phase "ERROR: FEATURE_wayland not ON after configure"
  grep -iE 'wayland' "$PREFIX/build-qtbase/configure.log" | tail -20 >&2 || true
  grep -E 'wayland|Wayland' "$PREFIX/build-qtbase/CMakeCache.txt" | head -20 >&2 || true
  exit 1
fi
grep -E '^FEATURE_wayland:|^QT_FEATURE_wayland:' "$PREFIX/build-qtbase/CMakeCache.txt"
log_phase "building qtbase (jobs=$JOBS) — typically 1–3 hours; log: $PREFIX/build-qtbase/build.log"
cmake --build "$PREFIX/build-qtbase" --parallel "$JOBS" 2>&1 | tee -a "$PREFIX/build-qtbase/build.log"
log_phase "installing qtbase ..."
cmake --install "$PREFIX/build-qtbase"
log_phase "qtbase install done"
