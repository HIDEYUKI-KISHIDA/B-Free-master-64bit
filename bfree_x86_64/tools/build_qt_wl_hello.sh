#!/usr/bin/env bash
# Link real QGuiApplication as p8test.elf (qt_wl_hello.elf).
# Not desktop.elf. Not libqbfree.a. Not BFREE_BOOT_GUI_FIRST.
# Cloud / a clean clone without the guest Qt prefix skips this; the C
# client (qt_wl_client.elf) stays in the p8test slot.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STUB="$ROOT/userland/compositor_stub"
DESK="$ROOT/userland/desktop_qt"
OUT="$STUB/qt_wl_hello.elf"

# WSL/maintainer: keep a copied prebuilt hello (skip failed link that restores .keep).
if [[ "${BFREE_FORCE_QT_HELLO_REBUILD:-0}" != "1" && -f "$OUT" ]]; then
  sz="$(wc -c < "$OUT")"
  if [[ "$sz" -gt 1000000 ]]; then
    echo "[qt_wl_hello] keep existing ELF ($sz bytes); BFREE_FORCE_QT_HELLO_REBUILD=1 to rebuild"
    exit 0
  fi
fi

if [[ -x "${HOME}/x86_64-elf-toolchain/bin/x86_64-elf-g++" ]]; then
  export PATH="${HOME}/x86_64-elf-toolchain/bin:${PATH:-}"
fi

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
if [[ -z "$GUEST_QT" ]]; then
  for d in "$HOME/out/bfree-qt6-guest-static" /root/out/bfree-qt6-guest-static; do
    if [[ -f "$d/lib/libQt6Gui.a" && -f "$d/lib/libQt6Core.a" ]]; then
      GUEST_QT="$d"
      break
    fi
  done
fi

need() {
  if [[ ! -f "$1" ]]; then
    echo "qt_wl_hello skip: missing $1" >&2
    echo "C p8test.elf stays the Wayland client. Guest Qt prefix is only on the maintainer tree." >&2
    exit 0
  fi
}

need_exec() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "qt_wl_hello skip: missing $1" >&2
    exit 0
  fi
}

need_exec x86_64-elf-g++
need_exec x86_64-elf-gcc
if [[ -z "$GUEST_QT" ]]; then
  echo "qt_wl_hello skip: no guest Qt prefix (libQt6Gui.a)" >&2
  exit 0
fi
need "$GUEST_QT/lib/libQt6Gui.a"
need "$GUEST_QT/lib/libQt6Core.a"
need "$DESK/crt0.o"
need "$DESK/guest_link_compat.o"
need "$DESK/guest_serial.o"
need "$DESK/desktop.ld"
need "$ROOT/tools/guest_desktop_link.sh"

export BFREE_QT_GUEST_BUILD_DIR="$GUEST_QT"
export BFREE_GUEST_CRT0="$DESK/crt0.o"
export BFREE_GUEST_COMPAT="$DESK/guest_link_compat.o"
export BFREE_GUEST_SERIAL="$DESK/guest_serial.o"
export BFREE_ROOT="$ROOT"

CXX=x86_64-elf-g++
QT_INC="$GUEST_QT/include"
QT_VER=6.8.0
if [[ -d "$QT_INC/QtCore/6.4.0" && ! -d "$QT_INC/QtCore/$QT_VER" ]]; then
  QT_VER=6.4.0
fi
for v in 6.8.0 6.7.3 6.6.3 6.5.3 6.4.2; do
  if [[ -d "$QT_INC/QtGui/$v" ]]; then
    QT_VER="$v"
    break
  fi
done

CXXFLAGS=(
  -fno-exceptions -fno-rtti -std=gnu++17 -O2 -fPIC -fno-stack-protector
  -DQT_NO_DEBUG -DQT_STATIC -DQT_GUI_LIB -DQT_CORE_LIB -DQT_NO_SSL
  -DQT_STATICPLUGIN -D__linux__ -D__x86_64__ -D_REENTRANT
  -I"$STUB"
  -I"$QT_INC"
  -I"$QT_INC/QtGui"
  -I"$QT_INC/QtCore"
  -I"$QT_INC/QtGui/$QT_VER"
  -I"$QT_INC/QtGui/$QT_VER/QtGui"
  -I"$QT_INC/QtCore/$QT_VER"
  -I"$QT_INC/QtCore/$QT_VER/QtCore"
  -I"$GUEST_QT/mkspecs/linux-g++"
)
if [[ -n "${BFREE_ELF_CXX_INCLUDE:-}" ]]; then
  CXXFLAGS+=(-isystem "$BFREE_ELF_CXX_INCLUDE")
fi
_inc="$(bash "$ROOT/tools/resolve_elf_cxx_include.sh" 2>/dev/null || true)"
if [[ -n "$_inc" ]]; then
  CXXFLAGS+=(-isystem "$_inc")
  for sub in x86_64-pc-elf x86_64-elf; do
    if [[ -f "$_inc/$sub/bits/c++config.h" ]]; then
      CXXFLAGS+=(-isystem "$_inc/$sub")
    fi
  done
fi
MUSL_INC=""
for musl in \
  "${BFREE_ELF_LIBM_DIR:-}/prefix/include" \
  "$ROOT/out/x86_64-elf-libm/prefix/include" \
  "$HOME/out/x86_64-elf-libm/prefix/include"; do
  if [[ -f "$musl/stdint.h" ]]; then
    MUSL_INC="$musl"
    CXXFLAGS+=(-idirafter "$musl")
    break
  fi
done

# p8test is APP: Linux mmap is 9, 26 is msync. Rebuild compat for hello only.
# Do not overwrite desktop_qt/guest_link_compat.o (desktop.elf).
COMPAT_HELLO="$STUB/guest_link_compat_hello.o"
if [[ -n "$MUSL_INC" && -f "$ROOT/tools/guest_link_compat.cpp" && -f "$DESK/guest_serial.h" ]]; then
  echo "[qt_wl_hello] compiling guest_link_compat for APP mmap (syscall 9 only, 32MiB ctor)"
  if "$CXX" -m64 -mcmodel=large -mno-red-zone -fno-stack-protector \
      -fno-pic -fno-exceptions -fno-rtti -O2 -std=gnu++17 \
      -isystem "$MUSL_INC" -D_GNU_SOURCE -D__linux__ -DBFREE_GUEST_APP_MMAP=1 \
      -c -o "$COMPAT_HELLO" "$ROOT/tools/guest_link_compat.cpp"; then
    export BFREE_GUEST_COMPAT="$COMPAT_HELLO"
  else
    echo "[qt_wl_hello] WARN: APP mmap compat compile failed — using $DESK/guest_link_compat.o" >&2
    export BFREE_GUEST_COMPAT="$DESK/guest_link_compat.o"
  fi
elif [[ -n "$MUSL_INC" && ! -f "$DESK/guest_serial.h" ]]; then
  echo "[qt_wl_hello] WARN: missing $DESK/guest_serial.h — using desktop guest_link_compat.o" >&2
  export BFREE_GUEST_COMPAT="$DESK/guest_link_compat.o"
fi

make -C "$STUB" wl_stub_flush.o
if ! "$CXX" "${CXXFLAGS[@]}" -c -o "$STUB/qbfree_wayland.o" "$STUB/qbfree_wayland.cpp"; then
  echo "qt_wl_hello skip: QPA compile failed. C p8test stays." >&2
  exit 0
fi

STUBS=()
# qt_futex + guest_mmap only. guest_platform_stub.o is the bfree QPA factory
# (QPlatformIntegrationPluginBFree / libqbfree.a) — do not link it here.
for o in "$DESK/qt_futex_guest_stub.o" "$DESK/guest_mmap.o"; do
  [[ -f "$o" ]] && STUBS+=("$o")
done

SYM_STUB="$STUB/qt_wl_hello_syms.o"
if ! x86_64-elf-gcc -ffreestanding -fno-pic -c -o "$SYM_STUB" "$STUB/qt_wl_hello_syms.c"; then
  echo "qt_wl_hello skip: sym stub compile failed. C p8test stays." >&2
  exit 0
fi
STUBS+=("$SYM_STUB")

BASE_ARCHIVES=(
  "$GUEST_QT/lib/libQt6Gui.a"
  "$GUEST_QT/lib/libQt6Core.a"
)
for a in \
  "$GUEST_QT/lib/libQt6BundledHarfbuzz.a" \
  "$GUEST_QT/lib/libQt6BundledFreetype.a" \
  "$GUEST_QT/lib/libQt6BundledLibpng.a" \
  "$GUEST_QT/lib/libQt6BundledZLIB.a" \
  "$GUEST_QT/lib/libQt6BundledPcre2.a"
do
  [[ -f "$a" ]] && BASE_ARCHIVES+=("$a")
done
for o in \
  "$GUEST_QT/lib/objects-Release/Gui_resources_1/.qt/rcc/qrc_qpdf_init.cpp.o" \
  "$GUEST_QT/lib/objects-Release/Gui_resources_2/.qt/rcc/qrc_gui_shaders_init.cpp.o"
do
  [[ -f "$o" ]] && BASE_ARCHIVES+=("$o")
done

echo "[qt_wl_hello] g++=$(command -v x86_64-elf-g++ 2>/dev/null || echo missing)"
echo "[qt_wl_hello] ld=$(command -v x86_64-elf-ld 2>/dev/null || echo missing)"
LINK_LOG="/tmp/qt_wl_hello.link.log"
GDL_LF="/tmp/bfree-guest_desktop_link.sh"
tr -d '\r' < "$ROOT/tools/guest_desktop_link.sh" > "$GDL_LF"

link_hello() {
  local out="$1"
  shift
  echo "[qt_wl_hello] linking $out (no libqbfree.a)"
  set +e
  bash "$GDL_LF" \
    -o "$out" \
    "-T$DESK/desktop.ld" \
    "${STUBS[@]}" \
    "$STUB/qbfree_wayland.o" \
    "$STUB/qt_wl_hello.o" \
    "$STUB/wl_stub_flush.o" \
    "$@" 2>&1 | tee "$LINK_LOG"
  link_rc=${PIPESTATUS[0]}
  set -e
  echo "[qt_wl_hello] link rc=$link_rc log_bytes=$(wc -c < "$LINK_LOG" | tr -d ' ')"
  if [[ "$link_rc" -ne 0 ]]; then
    echo "qt_wl_hello: unique undefined refs:" >&2
    grep -E 'undefined reference|undefined symbol|ld: error:|file not recognized|invalid option|not found|failed' "$LINK_LOG" \
      | sort -u | head -40 >&2 || true
    echo "qt_wl_hello: full log $LINK_LOG" >&2
    return 1
  fi
  sz="$(wc -c < "$out")"
  if [[ "$sz" -lt 1000000 ]]; then
    echo "qt_wl_hello: ELF too small ($sz)" >&2
    rm -f "$out"
    return 1
  fi
  return 0
}

HELLO_D2B=0
KEEP_ELF="$STUB/qt_wl_hello.elf.keep"
if [[ -f "$STUB/qt_wl_hello.elf" ]]; then
  cp -f "$STUB/qt_wl_hello.elf" "$KEEP_ELF"
fi

if [[ "${BFREE_D2B_QML:-1}" != "0" && -f "$GUEST_QT/lib/libQt6Qml.a" ]]; then
  echo "[qt_wl_hello] D2b: try QQmlEngine (no beginCreate, no Quick types)"
  CXXFLAGS_D2B=(
    "${CXXFLAGS[@]}"
    -DBFREE_D2B_QML=1 -DQT_QML_LIB
    -I"$QT_INC/QtQml"
    -I"$QT_INC/QtQml/$QT_VER"
    -I"$QT_INC/QtQml/$QT_VER/QtQml"
    -I"$QT_INC/QtQmlIntegration"
    -I"$QT_INC/QtQmlMeta"
    -I"$QT_INC/QtQmlMeta/$QT_VER"
    -I"$QT_INC/QtQmlMeta/$QT_VER/QtQmlMeta"
    -I"$QT_INC/QtQmlModels"
    -I"$QT_INC/QtQmlModels/$QT_VER"
    -I"$QT_INC/QtQmlModels/$QT_VER/QtQmlModels"
    -I"$QT_INC/QtQmlWorkerScript"
    -I"$QT_INC/QtQmlWorkerScript/$QT_VER"
    -I"$QT_INC/QtQmlWorkerScript/$QT_VER/QtQmlWorkerScript"
  )
  D2B_ARCHIVES=()
  for a in \
    "$GUEST_QT/lib/libQt6QmlMeta.a" \
    "$GUEST_QT/lib/libQt6QmlModels.a" \
    "$GUEST_QT/lib/libQt6QmlWorkerScript.a" \
    "$GUEST_QT/lib/libQt6Qml.a"
  do
    [[ -f "$a" ]] && D2B_ARCHIVES+=("$a")
  done
  if "$CXX" "${CXXFLAGS_D2B[@]}" -c -o "$STUB/qt_wl_hello.o" "$STUB/qt_wl_hello.cpp"; then
    if link_hello "$STUB/qt_wl_hello.d2b.elf" "${D2B_ARCHIVES[@]}" "${BASE_ARCHIVES[@]}"; then
      mv -f "$STUB/qt_wl_hello.d2b.elf" "$STUB/qt_wl_hello.elf"
      HELLO_D2B=1
      echo "[qt_wl_hello] D2b link ok"
    else
      echo "[qt_wl_hello] D2b link failed — fall back to D2 bits-only hello" >&2
      rm -f "$STUB/qt_wl_hello.d2b.elf"
    fi
  else
    echo "[qt_wl_hello] D2b compile failed — fall back to D2 bits-only hello" >&2
  fi
else
  echo "[qt_wl_hello] D2b skip (BFREE_D2B_QML=0 or no libQt6Qml.a)"
fi

if [[ "$HELLO_D2B" != "1" ]]; then
  if ! "$CXX" "${CXXFLAGS[@]}" -c -o "$STUB/qt_wl_hello.o" "$STUB/qt_wl_hello.cpp"; then
    echo "qt_wl_hello skip: hello compile failed. C p8test stays." >&2
    if [[ -f "$KEEP_ELF" ]]; then
      mv -f "$KEEP_ELF" "$STUB/qt_wl_hello.elf"
      echo "[qt_wl_hello] restored previous ELF" >&2
    fi
    exit 0
  fi
  if ! link_hello "$STUB/qt_wl_hello.elf" "${BASE_ARCHIVES[@]}"; then
    echo "qt_wl_hello skip: link failed. C p8test stays." >&2
    if [[ -f "$KEEP_ELF" ]]; then
      mv -f "$KEEP_ELF" "$STUB/qt_wl_hello.elf"
      echo "[qt_wl_hello] restored previous ELF" >&2
    fi
    exit 0
  fi
fi
rm -f "$KEEP_ELF"

sz="$(wc -c < "$STUB/qt_wl_hello.elf")"
echo "QT_WL_HELLO=$STUB/qt_wl_hello.elf"
echo "QT_WL_HELLO_BYTES=$sz"
echo "QT_WL_HELLO_STAMP=hybrid-qpa"
if [[ "$HELLO_D2B" == "1" ]]; then
  echo "QT_WL_HELLO_D2B=engine"
else
  echo "QT_WL_HELLO_D2B=bits-only"
fi
if [[ "$sz" -lt 1000000 ]]; then
  echo "qt_wl_hello: ELF too small ($sz) — not a linked QGuiApplication" >&2
  rm -f "$STUB/qt_wl_hello.elf"
  exit 0
fi
