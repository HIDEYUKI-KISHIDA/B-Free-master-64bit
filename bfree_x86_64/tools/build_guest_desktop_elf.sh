#!/usr/bin/env bash
# B4b-guest / B5b — build userland/desktop_qt/desktop.elf when guest Qt exists.
# From /mnt/c with CRLF: bash -c 'tr -d "\r" < tools/build_guest_desktop_elf.sh | bash -s'
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export BFREE_ROOT="$ROOT"
# shellcheck source=bfree_desktop_elf_verify.sh
source <(sed 's/\r$//' "$ROOT/tools/bfree_desktop_elf_verify.sh")

# Lordmilko first — never prefer /usr/local/x86_64-elf (Windows as → Exec format error on WSL).
export PATH="/root/x86_64-elf-toolchain/bin:${HOME}/x86_64-elf-toolchain/bin:/root/bin:${PATH:-}"
unset BFREE_ELF_GCC_ROOT BFREE_X86_64_ELF_TOOLS BFREE_ELF_CC BFREE_ELF_CXX || true
BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh") || {
  echo "[B4b-guest] ERROR: working x86_64-elf toolchain required." >&2
  echo "  export PATH=/root/x86_64-elf-toolchain/bin:\$PATH" >&2
  echo "  bash $ROOT/tools/install_x86_64_elf_gpp_prebuilt.sh" >&2
  exit 1
}
echo "[B4b-guest] toolchain: ${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}"

# Prefer musl libc built on Linux ext4 (partial /mnt/c trees often break configure).
for _musl in /root/out/x86_64-elf-libm "$ROOT/out/x86_64-elf-libm"; do
  if [[ -f "$_musl/libc.a" && -f "$_musl/libm.a" ]]; then
    export BFREE_ELF_LIBM_DIR="$_musl"
    echo "[B4b-guest] musl libc: $_musl/libc.a"
    break
  fi
done

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-/root/out/bfree-qt6-guest-static}"

if [[ ! -f "$GUEST_QT/lib/libQt6Core.a" ]]; then
  echo "[B4b-guest] guest Qt not found: $GUEST_QT/lib/libQt6Core.a" >&2
  echo "Build guest Qt first (hours):" >&2
  echo "  export BFREE_QT_GUEST_BUILD_DIR=$GUEST_QT" >&2
  echo "  bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  exit 1
fi

if [[ -f "$GUEST_QT/lib/libQt6Core.prl" ]] && grep -q 'glib-2.0' "$GUEST_QT/lib/libQt6Core.prl"; then
  echo "[B4b-guest] ERROR: guest Qt was built for Linux (glib in libQt6Core.prl)." >&2
  echo "  Rebuild required (hours, on /root/out):" >&2
  echo "    export BFREE_QT_SRC=/root/src/qt6" >&2
  echo "    export BFREE_QT_GUEST_BUILD_DIR=$GUEST_QT" >&2
  echo "    bash $ROOT/tools/rebuild_guest_qt_minimal.sh" >&2
  echo "  Then re-run: bash $ROOT/tools/build_usb_guest_qml_iso.sh" >&2
  exit 1
fi

export BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT"
export BFREE_QPA_BUILD="${BFREE_QPA_BUILD:-$ROOT/gui_server/integration_gui/bfree_qpa/build}"

_resolve() {
  bash -c 'tr -d "\r" < "$1" | bash -s -- "$2" "$3"' _ \
    "$ROOT/tools/resolve_elf_runtime_libs.sh" \
    "${BFREE_ELF_CC:-$(command -v x86_64-elf-gcc)}" \
    "${BFREE_ELF_CXX:-$(command -v x86_64-elf-g++)}"
}
if ! _resolve >/dev/null 2>&1; then
  echo "[B4b-guest] musl libc.a missing — building musl libc for x86_64-elf (CC=$BFREE_ELF_CC) ..."
  BFREE_ELF_CC="${BFREE_ELF_CC}" BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}" \
    bash -c 'tr -d "\r" < "$1" | bash -s' _ "$ROOT/tools/build_x86_64_elf_libm.sh" || {
    echo "[B4b-guest] ERROR: musl build failed. Use Linux toolchain + ext4 out dir:" >&2
    echo "  export PATH=/root/x86_64-elf-toolchain/bin:\$PATH" >&2
    echo "  export BFREE_ELF_LIBM_DIR=/root/out/x86_64-elf-libm" >&2
    echo "  bash $ROOT/tools/build_x86_64_elf_libm.sh" >&2
    exit 1
  }
fi
if ! _resolve >/dev/null; then
  echo "[B4b-guest] ERROR: x86_64-elf libc.a still missing after build_x86_64_elf_libm.sh" >&2
  exit 1
fi
_rt="$(_resolve)" || true
case "$_rt" in *libstdc++*) ;; *)
  echo "[B4b-guest] libstdc++.a missing — running install_x86_64_elf_libstdcxx.sh ..."
  bash -c 'tr -d "\r" < "$1" | bash -s' _ "$ROOT/tools/install_x86_64_elf_libstdcxx.sh"
  _rt="$(_resolve)"
  case "$_rt" in *libstdc++*) ;; *)
    echo "[B4b-guest] ERROR: libstdc++.a still missing" >&2
    exit 1
    ;;
  esac
  ;;
esac
echo "[B4b-guest] elf runtime: $_rt"

# libstdc++ headers (chrono) — often NOT beside lordmilko g++; use tree that owns libstdc++.a
_std="$(echo "$_rt" | tr ' ' '\n' | grep -E 'libstdc\+\+\.a$' | head -1 || true)"
[[ -n "$_std" && -f "$_std" ]] && export BFREE_ELF_LIBSTDCXX_PATH="$_std"
_inc="$(BFREE_ROOT="$ROOT" bash "$ROOT/tools/resolve_elf_cxx_include.sh")" || {
  echo "[B4b-guest] ERROR: libstdc++ include/c++ not found (need <chrono> for guest_link_compat.cpp)" >&2
  echo "  Try: sudo apt install g++-x86-64-elf" >&2
  echo "  Or:  bash tools/build_x86_64_elf_libstdcxx.sh" >&2
  echo "  Then: export BFREE_ELF_CXX_INCLUDE=\$(bash tools/resolve_elf_cxx_include.sh)" >&2
  exit 1
}
export BFREE_ELF_CXX_INCLUDE="$_inc"
export BFREE_ELF_LIBM_DIR="${BFREE_ELF_LIBM_DIR:-$ROOT/out/x86_64-elf-libm}"
echo "[B4b-guest] libstdc++ headers: $_inc"
echo "[B4b-guest] musl headers: $BFREE_ELF_LIBM_DIR/prefix/include"

export PATH="$GUEST_QT/bin:$PATH"

bfree_qpa_has_guest_input() {
  local lib="$1"
  [[ -f "$lib" ]] || return 1
  if command -v strings >/dev/null 2>&1; then
    if strings "$lib" 2>/dev/null | grep -q 'qt_static_plugin_QPlatformIntegrationPluginBFree'; then
      return 0
    fi
    if strings "$lib" 2>/dev/null | grep -qE 'BFreeGuest|QEventDispatcherBFreeGuest'; then
      return 0
    fi
  fi
  if grep -aqE 'BFreeGuest|QEventDispatcherBFreeGuest' "$lib" 2>/dev/null; then
    return 0
  fi
  if command -v nm >/dev/null 2>&1; then
    if nm "$lib" 2>/dev/null | grep -q 'QEventDispatcherBFreeGuest'; then
      return 0
    fi
  fi
  return 1
}

echo "[B4b-guest] guest Qt: $GUEST_QT"
bash "$ROOT/tools/patch_qt6core_no_rdrnd.sh" "$GUEST_QT/lib/libQt6Core.a"

GUEST_ELF_CXXFLAGS=""
if [[ "${BFREE_GUEST_WAYLAND_CLIENT:-0}" == "1" ]]; then
  GUEST_ELF_CXXFLAGS="-DBFREE_GUEST_WAYLAND_CLIENT=1"
  echo "[B4b-guest] Wayland client mode (skip libqbfree build)"
else
  echo "[B4b-guest] building libqbfree (CONFIG+=guestinput, always refresh)..."
  mkdir -p "$BFREE_QPA_BUILD"
  (cd "$BFREE_QPA_BUILD" && make clean 2>/dev/null || true)
  (cd "$BFREE_QPA_BUILD" && "$GUEST_QT/bin/qmake6" ../qpa_bfree.pro CONFIG+=guestinput && make -j"$(nproc 2>/dev/null || echo 4)")
  _qpa="$BFREE_QPA_BUILD/plugins/platforms/libqbfree.a"
  if ! bfree_qpa_has_guest_input "$_qpa"; then
    echo "[B4b-guest] ERROR: $_qpa missing guest dispatcher (rebuild with CONFIG+=guestinput)" >&2
    exit 1
  fi
  echo "[B4b-guest] libqbfree guestinput OK"
fi

echo "[B4b-guest] sync qtdeclarative mkspecs/libs into guest prefix (qmake QT += quick)..."
BD="$GUEST_QT/build-qtdeclarative"
if [[ ! -d "$BD" ]]; then
  echo "[B4b-guest] missing $BD" >&2
  exit 1
fi
mkdir -p "$GUEST_QT/lib" "$GUEST_QT/mkspecs/modules" "$GUEST_QT/lib/cmake"
cp -an "$BD/lib"/libQt6Qml*.a "$BD/lib"/libQt6Quick*.a "$GUEST_QT/lib/" 2>/dev/null || true
cp -an "$BD/mkspecs/modules"/qt_lib_qml*.pri "$BD/mkspecs/modules"/qt_lib_quick*.pri \
  "$GUEST_QT/mkspecs/modules/" 2>/dev/null || true
for mod in Qml QmlModels QmlMeta QmlCore Quick QuickTemplates2 QmlWorkerScript QmlLocalStorage; do
  if [[ -d "$BD/lib/cmake/Qt6${mod}" ]]; then
    rm -rf "$GUEST_QT/lib/cmake/Qt6${mod}"
    cp -a "$BD/lib/cmake/Qt6${mod}" "$GUEST_QT/lib/cmake/"
  fi
done
if [[ ! -f "$GUEST_QT/mkspecs/modules/qt_lib_quick.pri" ]]; then
  echo "[B4b-guest] missing qt_lib_quick.pri — build Quick in $BD" >&2
  exit 1
fi
echo "[B4b-guest] qt_lib_quick.pri OK"

echo "[B4b-guest] DesktopShell.qml qrc..."
BFREE_ROOT="$ROOT" bash "$ROOT/tools/gen_guest_desktop_qrc.sh"
test -f "$ROOT/userland/desktop_qt/guest_desktop.qrc" || {
  echo "[B4b-guest] ERROR: guest_desktop.qrc missing after gen" >&2
  exit 1
}
wc -l "$ROOT/userland/desktop_qt/guest_desktop.qrc"

echo "[B4b-guest] linking guest desktop.elf (DesktopShell.qml; may take 10-40 min)..."
if [[ -f "$ROOT/userland/desktop_qt/desktop.elf" ]]; then
  python3 "$ROOT/tools/update_guest_phdrs.py" "$ROOT/userland/desktop_qt/desktop.elf" || true
fi
# Force guest_link_compat.o relink when SKIP flag changes (drvfs stamp mtime can lie).
rm -f "$ROOT/userland/desktop_qt/guest_link_compat.o" "$ROOT/userland/desktop_qt/.guest_link_compat.flags"
make -C "$ROOT/userland/desktop_qt" -f Makefile.bfree guest-elf \
  BFREE_SKIP_GUEST_QRC=1 \
  BFREE_ELF_CXX_INCLUDE="$BFREE_ELF_CXX_INCLUDE" \
  BFREE_ELF_LIBM_DIR="$BFREE_ELF_LIBM_DIR" \
  BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT" \
  BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}" \
  CXXFLAGS="${GUEST_ELF_CXXFLAGS:-}"

if [[ "${BFREE_GUEST_WAYLAND_CLIENT:-0}" != "1" ]]; then
if ! bfree_elf_has_static_qpa_plugin "$ROOT/userland/desktop_qt/desktop.elf"; then
  echo "[B4b-guest] ERROR: desktop.elf missing Q_IMPORT_PLUGIN(QPlatformIntegrationPluginBFree)" >&2
  echo "[B4b-guest] hint: Makefile.bfree must regenerate desktop_plugin_import.cpp with bfree QPA" >&2
  _nm="$(bfree_find_elf_nm)"
  if [[ -n "$_nm" ]]; then
    "$_nm" "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null | grep 'qt_static_plugin' | head -5 >&2 || true
  fi
  strings "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null | grep -E 'BFreeGuest|QEventDispatcherBFreeGuest|qt_static_plugin' | head -5 >&2 || true
  exit 1
fi
if ! bfree_qpa_has_guest_input "$ROOT/userland/desktop_qt/desktop.elf"; then
  echo "[B4b-guest] ERROR: desktop.elf missing guest QPA dispatcher symbols" >&2
  exit 1
fi
if ! strings "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null | grep -q 'init_array: C runner begin'; then
  echo "[B4b-guest] WARN: desktop.elf missing init_array runner string (guest_link_compat.o may be stale)" >&2
fi
else
  echo "[B4b-guest] Wayland client link — skipped bfree QPA plugin checks"
fi

if python3 "$ROOT/tools/update_guest_phdrs.py" "$ROOT/userland/desktop_qt/desktop.elf"; then
  echo "[B4b-guest] refreshing guest_link_compat.o after PHDR sync..."
  rm -f "$ROOT/userland/desktop_qt/guest_link_compat.o"
  make -C "$ROOT/userland/desktop_qt" -f Makefile.bfree guest-elf \
    BFREE_SKIP_GUEST_QRC=1 \
    BFREE_ELF_CXX_INCLUDE="$BFREE_ELF_CXX_INCLUDE" \
    BFREE_ELF_LIBM_DIR="$BFREE_ELF_LIBM_DIR" \
    BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT" \
    BFREE_QT_BUILD_DIR="${BFREE_QT_BUILD_DIR:-$HOME/out/bfree-qt6-static}"
  python3 "$ROOT/tools/update_guest_phdrs.py" "$ROOT/userland/desktop_qt/desktop.elf" || true
fi

echo "[B4b-guest] done:"
ls -la "$ROOT/userland/desktop_qt/desktop.elf"
_build_tag="$(strings "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null | grep -E 'build=mmap96-v[0-9]+ staged' | tail -1 || true)"
if [[ -n "$_build_tag" ]]; then
  echo "[B4b-guest] build tag: $_build_tag"
else
  echo "[B4b-guest] WARN: no mmap96 build tag in desktop.elf" >&2
fi
_sz=$(stat -c%s "$ROOT/userland/desktop_qt/desktop.elf" 2>/dev/null || echo 0)
if [[ "$_sz" -ge 100000 ]]; then
  cp -f "$ROOT/userland/desktop_qt/desktop.elf" "$ROOT/userland/desktop_qt/desktop.elf.qt"
  echo "[B4b-guest] backup: userland/desktop_qt/desktop.elf.qt ($_sz bytes)"
fi
echo ""
echo "ISO: bash $ROOT/build_mvp_native_desktop.sh run"
