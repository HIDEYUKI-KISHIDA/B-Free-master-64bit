#!/usr/bin/env bash
# Rewrite build-qtbase/toolchain.cmake with -nostdinc and reconfigure (fixes host glibc header leak).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=tools/guest_qtbase_write_toolchain.sh
source "$ROOT/tools/guest_qtbase_write_toolchain.sh"

PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
BD="$PREFIX/build-qtbase"
WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
LIBFFI_PREFIX="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"

[[ -f "$BD/CMakeCache.txt" ]] || {
  echo "[fix-nostdinc] ERROR: missing $BD/CMakeCache.txt" >&2
  exit 1
}

resolve_musl_prefix() {
  local base="${BFREE_ELF_LIBM_DIR:-${HOME}/out/x86_64-elf-libm}"
  if [[ -f "$base/prefix/include/stdint.h" ]]; then echo "$base/prefix"
  elif [[ -f "$base/include/stdint.h" ]]; then echo "$base"
  elif [[ -f "$ROOT/out/x86_64-elf-libm/prefix/include/stdint.h" ]]; then echo "$ROOT/out/x86_64-elf-libm/prefix"
  else echo "$base/prefix"; fi
}

resolve_elf_cxx_include() {
  local gxx root inc target
  gxx="$(command -v x86_64-elf-g++)"
  root="$(cd "$(dirname "$gxx")/.." && pwd)"
  for inc in \
    "${BFREE_ELF_CXX_INCLUDE:-}" \
    "$(dirname "$(dirname "$("$gxx" -print-file-name=include/c++)" 2>/dev/null || true)")" \
    "$root/lib/gcc/x86_64-elf/"*/include/c++ \
    "$root/include/c++/13.2.0"; do
    [[ -n "$inc" && -f "$inc/atomic" ]] || continue
    for target in "$inc/x86_64-elf" "$inc/x86_64-pc-elf"; do
      [[ -d "$target" ]] && { echo "$inc|$target"; return 0; }
    done
    echo "$inc|"; return 0
  done
  return 1
}

MUSL_PREFIX="$(resolve_musl_prefix)"
LIBGCC_DIR="$(dirname "$(x86_64-elf-g++ -print-file-name=libgcc.a)")"
ELF_ROOT="$(cd "$(dirname "$(command -v x86_64-elf-g++)")/.." && pwd)"
CXX_INC_PAIR="$(resolve_elf_cxx_include)"
ELF_CXX_INC="${CXX_INC_PAIR%%|*}"
ELF_CXX_TARGET="${CXX_INC_PAIR#*|}"
EXTRA_ROOT=";${WAYLAND_PREFIX};${LIBFFI_PREFIX}"

export BFREE_QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
bash "$ROOT/tools/patch_qt_guest_disable_udev.sh"

echo "[fix-nostdinc] rewriting $BD/toolchain.cmake (musl=$MUSL_PREFIX)"
guest_qtbase_write_toolchain_cmake "$BD/toolchain.cmake" \
  "$MUSL_PREFIX" "$LIBGCC_DIR" "$ELF_ROOT" "$ELF_CXX_INC" "$ELF_CXX_TARGET" "$EXTRA_ROOT"

purge_input_feature_cache() {
  local cache="$BD/CMakeCache.txt" tmp
  [[ -f "$cache" ]] || return 0
  tmp="$(mktemp)"
  grep -viE \
    '^FEATURE_libudev:|^QT_FEATURE_libudev:|^FEATURE_libinput:|^QT_FEATURE_libinput:|^FEATURE_evdev:|^QT_FEATURE_evdev:|PKGCONFIG_.*libudev|LIBUDEV|libudev\.pc' \
    "$cache" >"$tmp" || true
  mv "$tmp" "$cache"
  echo "[fix-nostdinc] purged libudev/libinput/evdev entries from CMakeCache.txt"
  rm -rf "$BD/src/platformsupport/devicediscovery/CMakeFiles" 2>/dev/null || true
}

verify_no_udev_in_build() {
  local cache="$BD/CMakeCache.txt" ninja="$BD/build.ninja"
  if grep -qE '^(FEATURE_libudev|QT_FEATURE_libudev).*ON' "$cache" 2>/dev/null; then
    echo "[fix-nostdinc] ERROR: libudev still ON in CMakeCache.txt" >&2
    grep -iE 'libudev|libinput|FEATURE_evdev' "$cache" >&2 || true
    return 1
  fi
  if [[ -f "$ninja" ]] && grep -q 'qdevicediscovery_udev.cpp' "$ninja"; then
    echo "[fix-nostdinc] ERROR: build.ninja still lists qdevicediscovery_udev.cpp" >&2
    grep 'qdevicediscovery_udev' "$ninja" | head -3 >&2 || true
    return 1
  fi
  echo "[fix-nostdinc] verify: libudev OFF, no udev.cpp in build.ninja"
}

purge_input_feature_cache

echo "[fix-nostdinc] reconfigure build-qtbase (nostdinc + disable host libudev/libinput/evdev) ..."
(
  cd "$BD"
  env -u PKG_CONFIG_PATH -u PKG_CONFIG_LIBDIR -u PKG_CONFIG_SYSROOT_DIR \
    cmake . \
    -DCMAKE_TOOLCHAIN_FILE="$BD/toolchain.cmake" \
    -DFEATURE_libudev=OFF \
    -DFEATURE_libinput=OFF \
    -DFEATURE_evdev=OFF \
    -DQT_FEATURE_libudev=OFF \
    -DQT_FEATURE_libinput=OFF \
    -DQT_FEATURE_evdev=OFF
)
verify_no_udev_in_build

echo "[fix-nostdinc] OK — fresh build log recommended:"
echo "  : > \"$BD/build.log\""
echo "  JOBS=4 bash tools/resume_guest_qtbase_wayland_build.sh"
