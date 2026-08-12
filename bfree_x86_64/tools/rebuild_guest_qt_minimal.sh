#!/usr/bin/env bash
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
# Rebuild guest Qt6 static WITHOUT host glib/ICU/network plugins (fixes desktop.elf link).
# Run on native Linux fs (/root/out), not under /mnt/c. Hours; use --parallel 2 if WSL OOMs.
#
#   export BFREE_QT_SRC=/root/src/qt6
#   export BFREE_QT_GUEST_BUILD_DIR=/root/out/bfree-qt6-guest-static
#   export PATH="/root/bin:/usr/local/x86_64-elf/bin:$PATH"
#   bash tools/rebuild_guest_qt_minimal.sh
#
# Then:
#   bash tools/build_usb_guest_qml_iso.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
PREFIX="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"
HOST_QT="${BFREE_QT_BUILD_DIR:-/root/out/bfree-qt6-static}"
JOBS="${JOBS:-2}"
MKSPEC_DIR="$ROOT/tools/qt-mkspecs/bfree-g++"
export PATH="/root/x86_64-elf-toolchain/bin:${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"

resolve_musl_prefix() {
  local base="${BFREE_ELF_LIBM_DIR:-${HOME}/out/x86_64-elf-libm}"
  if [[ -f "$base/prefix/include/stdint.h" ]]; then
    echo "$base/prefix"
  elif [[ -f "$base/include/stdint.h" ]]; then
    echo "$base"
  elif [[ -f "$ROOT/out/x86_64-elf-libm/prefix/include/stdint.h" ]]; then
    echo "$ROOT/out/x86_64-elf-libm/prefix"
  else
    echo "$base/prefix"
  fi
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
  for p in \
    "${BFREE_QT_BUILD_DIR:-}" \
    "$HOST_QT" \
    /root/out/bfree-qt6-static \
    "${HOME}/out/bfree-qt6-static"; do
    if host_qt_ok "$p"; then
      echo "$p"
      return 0
    fi
  done
  return 1
}

ensure_host_qt() {
  local resolved
  resolved="$(resolve_host_qt || true)"
  if [[ -n "$resolved" ]]; then
    HOST_QT="$resolved"
    echo "[ok] host Qt (QT_HOST_PATH): $HOST_QT"
    return 0
  fi
  echo "[host-qt] building native qtbase for cross-compile (QT_HOST_PATH) ..."
  env BFREE_QT_SRC="$QT_SRC" BFREE_QT_BUILD_DIR="$HOST_QT" JOBS="$JOBS" \
    bash "$ROOT/tools/build_host_qt_minimal.sh"
  resolved="$(resolve_host_qt || true)"
  if [[ -z "$resolved" ]]; then
    echo "[FAIL] host Qt still missing after build_host_qt_minimal.sh" >&2
    echo "  export BFREE_QT_BUILD_DIR=$HOST_QT" >&2
    echo "  bash $ROOT/tools/build_host_qt_minimal.sh" >&2
    exit 1
  fi
  HOST_QT="$resolved"
  echo "[ok] host Qt (QT_HOST_PATH): $HOST_QT"
}

ensure_libstdcxx() {
  if libstdcxx_ok; then
    echo "[ok] libstdc++ for $(command -v x86_64-elf-g++)"
    return 0
  fi
  echo "[FAIL] x86_64-elf-g++ cannot link — missing libstdc++.a" >&2
  echo "  Run (as root, 1–2 hours):" >&2
  echo "    export BFREE_LINUX_BUILD_ROOT=/root/bfree-native-build" >&2
  echo "    export BFREE_ELF_MUSL_SYSROOT=/root/out/x86_64-elf-libm/prefix" >&2
  echo "    export PATH=/root/x86_64-elf-toolchain/bin:\$PATH" >&2
  echo "    bash $ROOT/tools/build_x86_64_elf_libstdcxx.sh" >&2
  echo "    bash $ROOT/tools/install_x86_64_elf_libstdcxx.sh" >&2
  exit 1
}

write_toolchain_cmake() {
  local out="$1" musl="$2" libgcc_dir="$3" elf_root="$4" cxx_inc="$5" cxx_target="$6"
  local extra_root="${7:-}"
  local wayland_prefix="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
  local libffi_prefix="${BFREE_ELF_LIBFFI_DIR:-$ROOT/out/x86_64-elf-libffi}"
  if [[ -z "$extra_root" && "${BFREE_QT_WAYLAND:-0}" != "0" && -f "$wayland_prefix/lib/libwayland-client.a" ]]; then
    extra_root=";${wayland_prefix};${libffi_prefix}"
  fi
  local cxx_extra=""
  if [[ -n "$cxx_inc" ]]; then
    cxx_extra=" -isystem ${cxx_inc}"
    if [[ -n "$cxx_target" ]]; then
      cxx_extra+=" -isystem ${cxx_target}"
    fi
  fi
  cxx_extra+=" -isystem ${musl}/include"
  cat >"$out" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_AR x86_64-elf-ar)
set(CMAKE_RANLIB x86_64-elf-ranlib)
set(CMAKE_STRIP x86_64-elf-strip)
set(CMAKE_FIND_ROOT_PATH "$elf_root;${musl}${extra_root}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_LIBRARY_PATH "$libgcc_dir;${musl}/lib")
set(CMAKE_INCLUDE_PATH "${musl}/include")
set(CMAKE_C_FLAGS "-isystem ${musl}/include -D__linux__ -D_GNU_SOURCE -L${musl}/lib -L${libgcc_dir}")
set(CMAKE_CXX_FLAGS "-D__linux__ -D_GNU_SOURCE -L${musl}/lib -L${libgcc_dir}${cxx_extra}")
set(CMAKE_EXE_LINKER_FLAGS "-L${libgcc_dir} -L${musl}/lib")
EOF
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
    "$root/include/c++/"* \
    /root/bfree-native-build/x86_64-elf-gcc-full/lib/gcc/x86_64-elf/*/include/c++ \
    /root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0; do
    [[ -n "$inc" && -f "$inc/atomic" ]] || continue
    for target in "$inc/x86_64-elf" "$inc/x86_64-pc-elf"; do
      if [[ -d "$target" ]]; then
        echo "$inc|$target"
        return 0
      fi
    done
    echo "$inc|"
    return 0
  done
  return 1
}

patch_qt_guest_linux_fs_h() {
  bash "$ROOT/tools/patch_qt_guest_linux_fs_h.sh"
}

ensure_guest_kernel_headers() {
  bash "$ROOT/tools/install_musl_kernel_uapi.sh"
  patch_qt_guest_linux_fs_h
}

need() { command -v "$1" >/dev/null 2>&1 || { echo "[FAIL] missing: $1" >&2; exit 1; }; }
need x86_64-elf-gcc
need x86_64-elf-g++
need perl
need cmake
need ninja

ensure_libstdcxx
ensure_host_qt
MUSL_PREFIX="$(resolve_musl_prefix)"
ensure_guest_kernel_headers "$MUSL_PREFIX"
LIBGCC_DIR="$(dirname "$(x86_64-elf-g++ -print-file-name=libgcc.a)")"
ELF_ROOT="$(cd "$(dirname "$(command -v x86_64-elf-g++)")/.." && pwd)"
CXX_INC_PAIR="$(resolve_elf_cxx_include || true)"
ELF_CXX_INC="${CXX_INC_PAIR%%|*}"
ELF_CXX_TARGET="${CXX_INC_PAIR#*|}"
if [[ -z "$ELF_CXX_INC" || ! -f "$ELF_CXX_INC/atomic" ]]; then
  echo "[FAIL] libstdc++ headers not found (need <atomic> for Qt configure)" >&2
  echo "  export BFREE_ELF_CXX_INCLUDE=/root/bfree-native-build/x86_64-elf-gcc-full/include/c++/13.2.0" >&2
  exit 1
fi
echo "  musl:     $MUSL_PREFIX"
echo "  libgcc:   $LIBGCC_DIR"
echo "  cxx inc:  $ELF_CXX_INC"
echo "  elf root: $ELF_ROOT"

if [ ! -f "$QT_SRC/qtbase/configure" ]; then
  echo "[FAIL] Qt source not found: $QT_SRC/qtbase/configure" >&2
  echo "  export BFREE_QT_SRC=/root/src/qt6 && bash tools/clone_qt6_src.sh" >&2
  exit 1
fi

mkdir -p "$MKSPEC_DIR"
cat >"$MKSPEC_DIR/qmake.conf" <<'EOF'
include(../../linux-g++/qmake.conf)
QMAKE_CC = x86_64-elf-gcc
QMAKE_CXX = x86_64-elf-g++
QMAKE_LINK = x86_64-elf-g++
QMAKE_AR = x86_64-elf-ar cqs
QMAKE_OBJCOPY = x86_64-elf-objcopy
QMAKE_STRIP = x86_64-elf-strip
EOF

echo "=== Rebuild minimal guest Qt ==="
echo "  sources:   $QT_SRC"
echo "  prefix:    $PREFIX"
echo "  host (QT_HOST_PATH): $HOST_QT"
echo "  jobs:      $JOBS"
echo ""
echo "This removes the old build-qtbase tree and reconfigures without glib/ICU/openssl/network."
# Auto-confirm for non-interactive builds
if [ -z "${BFREE_AUTO_CONFIRM:-}" ]; then
  read -r -p "Continue? [y/N] " ans
  case "$ans" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 0 ;;
  esac
fi

rm -rf "$PREFIX/build-qtbase"
mkdir -p "$PREFIX/build-qtbase"
cd "$PREFIX/build-qtbase"

WAYLAND_PREFIX="${BFREE_ELF_WAYLAND_DIR:-$ROOT/out/x86_64-elf-wayland}"
WAYLAND_CMAKE_ARGS=()
if [[ "${BFREE_QT_WAYLAND:-auto}" != "0" && -f "$WAYLAND_PREFIX/lib/libwayland-client.a" ]]; then
  SCANNER="$(command -v wayland-scanner 2>/dev/null || true)"
  if [[ -n "$SCANNER" ]]; then
    bash "$ROOT/tools/install_wayland_cmake_configs.sh" "$SCANNER"
    WAYLAND_CMAKE_ARGS=(-DWayland_DIR="$WAYLAND_PREFIX/lib/cmake/Wayland")
    echo "  wayland:  $WAYLAND_PREFIX (Gui FEATURE_wayland)"
  fi
fi

write_toolchain_cmake "$PREFIX/build-qtbase/toolchain.cmake" "$MUSL_PREFIX" "$LIBGCC_DIR" "$ELF_ROOT" "$ELF_CXX_INC" "$ELF_CXX_TARGET"

# Direct CMake invocation for cross-compilation
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
  "${WAYLAND_CMAKE_ARGS[@]}" \
  "$QT_SRC/qtbase"

cmake --build . --parallel "$JOBS"
cmake --install .

echo ""
echo "=== qtshadertools ==="
mkdir -p "$PREFIX/build-qtshadertools"
cd "$PREFIX/build-qtshadertools"
"$PREFIX/bin/qt-cmake" "$QT_SRC/qtshadertools" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
cmake --build . --parallel "$JOBS"
cmake --install .

echo ""
echo "=== qtdeclarative (Qml + Quick only) ==="
rm -rf "$PREFIX/build-qtdeclarative"
mkdir -p "$PREFIX/build-qtdeclarative"
cd "$PREFIX/build-qtdeclarative"
"$PREFIX/bin/qt-cmake" "$QT_SRC/qtdeclarative" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DQT_BUILD_TOOLS=OFF \
  -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF \
  -DFEATURE_qml_profiler=OFF \
  -DFEATURE_qmlpreview=OFF
cmake --build . --target Qml QmlModels QmlMeta QmlCore Quick --parallel "$JOBS"
cmake --install . || true
cp -an "$PREFIX/build-qtdeclarative/lib"/libQt6Qml*.a \
       "$PREFIX/build-qtdeclarative/lib"/libQt6Quick*.a \
       "$PREFIX/lib/" 2>/dev/null || true
cp -an "$PREFIX/build-qtdeclarative/mkspecs/modules"/qt_lib_qml*.pri \
       "$PREFIX/build-qtdeclarative/mkspecs/modules"/qt_lib_quick*.pri \
       "$PREFIX/mkspecs/modules/" 2>/dev/null || true

echo ""
echo "=== Verify (no glib in Core prl) ==="
if grep -q 'glib-2.0' "$PREFIX/lib/libQt6Core.prl" 2>/dev/null; then
  echo "[WARN] libQt6Core.prl still mentions glib — link may still fail." >&2
  grep 'LIBS' "$PREFIX/lib/libQt6Core.prl" | head -3 || true
else
  echo "[ok] libQt6Core.prl has no glib-2.0"
fi
ls -la "$PREFIX/lib/libQt6Core.a" "$PREFIX/lib/libQt6Quick.a"

echo ""
echo "Next: cd $ROOT && bash tools/build_usb_guest_qml_iso.sh"
