#!/usr/bin/env bash
# Relink desktop.elf for D3 Wayland vfork client (stub compositor path).
# Links qbfree_wayland.o + wl_stub_flush.o + libqbfree.a (symbol refs only;
# no --whole-archive — runtime selects stub wayland QPA), -DBFREE_D3_WAYLAND_QPA.
# Requires maintainer guest Qt prefix + desktop_qt .o tree (from Program/ or bfree_build).
# Does not overwrite daily bfree.iso.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
STUB="$ROOT/userland/compositor_stub"
cd "$DESK"

echo "[d3-desktop] git=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"

D3_NEED_MARK='phdr-text-v1'
if ! grep -qF "$D3_NEED_MARK" "$ROOT/tools/guest_link_compat.cpp"; then
  echo "FAIL: tools/guest_link_compat.cpp lacks $D3_NEED_MARK (git pull blocked?)" >&2
  echo "  cd $ROOT && bash tools/wsl_sync_d3_branch.sh" >&2
  echo "  rm -f userland/desktop_qt/guest_link_compat.o && bash tools/build_desktop_d3_wayland.sh" >&2
  exit 1
fi
if ! grep -qF 'bfree_guest_fill_auxv_core' "$ROOT/tools/guest_link_compat.cpp"; then
  echo "FAIL: guest_link_compat.cpp missing bfree_guest_fill_auxv_core (stale local edit)" >&2
  echo "  cd $ROOT && bash tools/wsl_sync_d3_branch.sh" >&2
  exit 1
fi
if [[ ! -x "$ROOT/tools/check_d3_desktop_main_tls.sh" ]]; then
  echo "FAIL: missing tools/check_d3_desktop_main_tls.sh — run: bash tools/wsl_sync_d3_branch.sh" >&2
  exit 1
fi

export PATH="${HOME}/x86_64-elf-toolchain/bin:/root/x86_64-elf-toolchain/bin:${PATH:-}"
export BFREE_ROOT="$ROOT"
export HOME="${HOME:-/home/h_kis}"
export BFREE_D3_WAYLAND_LINK=1

GUEST_QT="${BFREE_QT_GUEST_BUILD_DIR:-}"
for d in "$HOME/out/bfree-qt6-guest-static" /root/out/bfree-qt6-guest-static; do
  if [[ -z "$GUEST_QT" && -f "$d/lib/libQt6Core.a" ]]; then
    GUEST_QT="$d"
  fi
done
export BFREE_QT_GUEST_BUILD_DIR="${GUEST_QT:-/root/out/bfree-qt6-guest-static}"

HOST_QT="${BFREE_QT_BUILD_DIR:-}"
for d in "$HOME/out/bfree-qt6-static" /root/out/bfree-qt6-static; do
  if [[ -z "$HOST_QT" && ( -x "$d/libexec/moc" || -x "$d/bin/moc" ) ]]; then
    HOST_QT="$d"
  fi
done
export BFREE_QT_BUILD_DIR="${HOST_QT:-$HOME/out/bfree-qt6-static}"

MUSL_OUT=""
MUSL_INC=""
for musl_root in "$HOME/out/x86_64-elf-libm" \
                 /root/out/x86_64-elf-libm \
                 "$ROOT/out/x86_64-elf-libm"; do
  if [[ -z "$MUSL_OUT" && -f "$musl_root/libc.a" && -f "$musl_root/libm.a" ]]; then
    MUSL_OUT="$musl_root"
  fi
  for musl_inc in "$musl_root/prefix/include" "$musl_root/include"; do
    if [[ -z "$MUSL_INC" && -f "$musl_inc/stdio.h" ]]; then
      MUSL_INC="$musl_inc"
    fi
  done
  [[ -n "$MUSL_OUT" && -n "$MUSL_INC" ]] && break
done
if [[ -n "$MUSL_OUT" ]]; then
  export BFREE_ELF_LIBM_DIR="$MUSL_OUT"
  export BFREE_ELF_LIBC_PATH="$MUSL_OUT/libc.a"
  export BFREE_ELF_LIBM_PATH="$MUSL_OUT/libm.a"
fi

SRC_FALLBACK="${BFREE_DESKTOP_OBJ_FALLBACK:-$HOME/bfree_build/userland/desktop_qt}"
PROGRAM_DESK="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt"
PROGRAM_ROOT="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64"
if [[ ! -d "$SRC_FALLBACK" && -d "$PROGRAM_DESK" ]]; then
  SRC_FALLBACK="$PROGRAM_DESK"
fi

need() {
  if [[ ! -e "$1" ]]; then
    echo "FAIL: missing $1" >&2
    exit 1
  fi
}

resolve_qpa_inc() {
  if [[ -n "${BFREE_QPA_INC:-}" && -f "${BFREE_QPA_INC}/bfree/bfree_guest_abi.h" ]]; then
    echo "$BFREE_QPA_INC"
    return 0
  fi
  local d
  for d in \
    "$ROOT/gui_server/integration_gui/bfree_qpa" \
    "$PROGRAM_ROOT/gui_server/integration_gui/bfree_qpa" \
    "$DESK/../../gui_server/integration_gui/bfree_qpa"; do
    if [[ -f "$d/bfree/bfree_guest_abi.h" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

restore_desk_missing() {
  local name src
  for name in "$@"; do
    [[ -f "$DESK/$name" ]] && continue
    for src in "$SRC_FALLBACK" "$PROGRAM_DESK" "$HOME/bfree_build/userland/desktop_qt"; do
      [[ -f "$src/$name" ]] || continue
      cp -f "$src/$name" "$DESK/$name"
      echo "  restore $name <= $src"
      break
    done
  done
}

echo "[d3-desktop] restore missing desktop_qt headers/inc from maintainer tree"
restore_desk_tree() {
  local dir f base
  for dir in "$SRC_FALLBACK" "$PROGRAM_DESK" "$HOME/bfree_build/userland/desktop_qt"; do
    [[ -d "$dir" ]] || continue
    for f in "$dir"/*.h "$dir"/*.inc; do
      [[ -f "$f" ]] || continue
      base="$(basename "$f")"
      [[ "$base" == "guest_resource_holder_va.h" ]] && continue
      [[ -f "$DESK/$base" ]] && continue
      cp -f "$f" "$DESK/$base"
      echo "  restore $base <= $dir"
    done
  done
  restore_desk_missing guest_serial.h guest_desktop_bridge.h guest_mvp_qmlcache_register.h \
    guest_breeze_tokens.h guest_mvp_shell_qml.inc
  local req
  for req in guest_desktop_bridge.h guest_mvp_qmlcache_register.h guest_breeze_tokens.h guest_serial.h; do
    if [[ ! -f "$DESK/$req" ]]; then
      echo "FAIL: missing $DESK/$req" >&2
      echo "  copy Program/bfree_x86_64/userland/desktop_qt/*.h into bfree-d2c, or set BFREE_DESKTOP_OBJ_FALLBACK" >&2
      return 1
    fi
  done
}

need "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a"
need "$ROOT/tools/guest_link_compat.cpp"
need "$STUB/qbfree_wayland.cpp"
need Makefile.guest-elf
if [[ -z "$MUSL_INC" ]]; then
  echo "FAIL: musl headers not found (need out/x86_64-elf-libm/prefix/include)" >&2
  exit 1
fi
if [[ -z "${BFREE_ELF_LIBC_PATH:-}" || ! -f "${BFREE_ELF_LIBC_PATH}" ]]; then
  echo "FAIL: musl libc.a not found (need ~/out/x86_64-elf-libm/libc.a)" >&2
  echo "  bash tools/build_x86_64_elf_libm.sh  or set BFREE_ELF_LIBM_DIR" >&2
  exit 1
fi

CXX=x86_64-elf-g++
QT_INC="$BFREE_QT_GUEST_BUILD_DIR/include"
QT_VER=6.8.0
for v in 6.8.0 6.7.3 6.6.3 6.5.3 6.4.2; do
  if [[ -d "$QT_INC/QtGui/$v" ]]; then
    QT_VER="$v"
    break
  fi
done

WL_CXXFLAGS=(
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
  -I"$BFREE_QT_GUEST_BUILD_DIR/mkspecs/linux-g++"
  -idirafter "$MUSL_INC"
)

_inc="$(bash "$ROOT/tools/resolve_elf_cxx_include.sh" 2>/dev/null || true)"
if [[ -n "$_inc" ]]; then
  WL_CXXFLAGS+=(-isystem "$_inc")
  for sub in x86_64-pc-elf x86_64-elf; do
    [[ -f "$_inc/$sub/bits/c++config.h" ]] && WL_CXXFLAGS+=(-isystem "$_inc/$sub")
  done
fi

makefile_guest_var() {
  grep -m1 "^$1" Makefile.guest-elf | sed "s/^$1[[:space:]]*=[[:space:]]*//"
}

makefile_guest_objects() {
  awk '
    /^OBJECTS[[:space:]]+=/ {
      line=$0
      sub(/^OBJECTS[[:space:]]+=[[:space:]]*/, "", line)
      gsub(/\\$/, "", line)
      gsub(/[[:space:]]+$/, "", line)
      printf "%s ", line
      inobj=1
      next
    }
    inobj && /^[[:space:]]+/ {
      line=$0
      gsub(/^[[:space:]]+/, "", line)
      gsub(/\\$/, "", line)
      gsub(/[[:space:]]+$/, "", line)
      printf "%s ", line
      next
    }
    inobj { exit }
  ' Makefile.guest-elf
}

restore_one_desk_file() {
  local name="$1"
  [[ -s "$DESK/$name" ]] && return 0
  local src
  for src in "$SRC_FALLBACK" "$PROGRAM_DESK" "$HOME/bfree_build/userland/desktop_qt"; do
    [[ -f "$src/$name" ]] || continue
    cp -f "$src/$name" "$DESK/$name"
    echo "  restore $name <= $src"
    return 0
  done
  return 1
}

restore_desk_objects() {
  echo "[d3-desktop] restore Makefile.guest-elf OBJECTS from maintainer tree"
  local objs o missing=()
  objs="$(makefile_guest_objects)"
  for o in $objs; do
    o="${o//$'\r'/}"
    [[ "$o" == "guest_main.o" || "$o" == "guest_link_compat.o" ]] && continue
    if [[ -s "$DESK/$o" ]]; then
      continue
    fi
    if restore_one_desk_file "$o"; then
      continue
    fi
    missing+=("$o")
  done
  for o in crt0.o guest_serial.o desktop.ld; do
    restore_one_desk_file "$o" || true
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    echo "FAIL: missing desktop_qt objects (need maintainer bfree_build or Program tree):" >&2
    printf '  %s\n' "${missing[@]}" >&2
    echo "  set BFREE_DESKTOP_OBJ_FALLBACK=/path/to/userland/desktop_qt" >&2
    return 1
  fi
}

resolve_libqbfree_a() {
  local d
  for d in \
    "$ROOT/gui_server/integration_gui/bfree_qpa/build/plugins/platforms/libqbfree.a" \
    "$PROGRAM_ROOT/gui_server/integration_gui/bfree_qpa/build/plugins/platforms/libqbfree.a" \
    "$HOME/bfree_build/gui_server/integration_gui/bfree_qpa/build/plugins/platforms/libqbfree.a"; do
    if [[ -f "$d" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

collect_d3_archives() {
  D3_ARCHIVES=()
  local qpa
  qpa="$(resolve_libqbfree_a)" || {
    echo "FAIL: libqbfree.a not found (build Program/bfree_qpa or set BFREE_QPA_BUILD)" >&2
    return 1
  }
  D3_ARCHIVES+=("$qpa")
  local a
  for a in \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/objects-Release/Gui_resources_1/.qt/rcc/qrc_qpdf_init.cpp.o" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/objects-Release/Gui_resources_2/.qt/rcc/qrc_gui_shaders_init.cpp.o" \
    "$BFREE_QT_GUEST_BUILD_DIR/plugins/imageformats/libqgif.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/plugins/imageformats/libqico.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/plugins/imageformats/libqjpeg.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledLibjpeg.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Quick.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Gui.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledHarfbuzz.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledFreetype.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledLibpng.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6QmlMeta.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6QmlModels.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6QmlWorkerScript.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Qml.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6Core.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledZLIB.a" \
    "$BFREE_QT_GUEST_BUILD_DIR/lib/libQt6BundledPcre2.a"; do
    [[ -f "$a" ]] && D3_ARCHIVES+=("$a")
  done
  [[ ${#D3_ARCHIVES[@]} -gt 0 ]] || {
    echo "FAIL: no Qt guest archives under $BFREE_QT_GUEST_BUILD_DIR/lib" >&2
    return 1
  }
}

resolve_host_moc() {
  local d m
  for d in "${BFREE_QT_BUILD_DIR:-}" "$HOME/out/bfree-qt6-static" /root/out/bfree-qt6-static; do
    [[ -n "$d" ]] || continue
    for m in "$d/libexec/moc" "$d/bin/moc"; do
      if [[ -x "$m" ]]; then
        echo "$m"
        return 0
      fi
    done
  done
  if command -v moc >/dev/null 2>&1; then
    command -v moc
    return 0
  fi
  return 1
}

compile_guest_shell_process_objects() {
  [[ -f "$DESK/guest_bfree_shell_process.cpp" ]] || return 0
  restore_desk_missing moc_predefs.h
  if [[ ! -f "$DESK/moc_predefs.h" ]]; then
    echo "FAIL: missing $DESK/moc_predefs.h (copy from bfree_build desktop_qt)" >&2
    return 1
  fi
  local mk_defines mk_cxx mk_cxxflags mk_incpath expanded_flags moc_bin
  mk_defines="$(makefile_guest_var DEFINES)"
  mk_cxx="$(makefile_guest_var CXX)"
  mk_cxxflags="$(makefile_guest_var CXXFLAGS)"
  mk_incpath="$(makefile_guest_var INCPATH)"
  [[ -n "$mk_cxx" && -n "$mk_cxxflags" ]] || return 0
  expanded_flags="${mk_cxxflags//\$(DEFINES)/$mk_defines}"
  if [[ -n "$MUSL_INC" ]]; then
    expanded_flags+=" -idirafter $MUSL_INC"
  fi
  echo "[d3-desktop] compile guest_bfree_shell_process.o + moc (pty shell syms)"
  rm -f guest_bfree_shell_process.o moc_guest_bfree_shell_process.o moc_guest_bfree_shell_process.cpp
  # shellcheck disable=SC2086
  $mk_cxx -c $expanded_flags $mk_incpath -I. -o guest_bfree_shell_process.o guest_bfree_shell_process.cpp
  if ! nm guest_bfree_shell_process.o 2>/dev/null | grep -q ' guest_pty_shell_start'; then
    echo "FAIL: guest_bfree_shell_process.o lacks guest_pty_shell_start" >&2
    return 1
  fi
  moc_bin="$(resolve_host_moc)" || {
    echo "FAIL: host moc not found (need ~/out/bfree-qt6-static/libexec/moc)" >&2
    return 1
  }
  echo "[d3-desktop] moc guest_bfree_shell_process.h via $moc_bin"
  # shellcheck disable=SC2086
  "$moc_bin" $mk_defines --include "$DESK/moc_predefs.h" $mk_incpath -I. \
    "$DESK/guest_bfree_shell_process.h" -o "$DESK/moc_guest_bfree_shell_process.cpp"
  # shellcheck disable=SC2086
  $mk_cxx -c $expanded_flags $mk_incpath -I. -o moc_guest_bfree_shell_process.o moc_guest_bfree_shell_process.cpp
}

link_d3_desktop() {
  local objs=() o
  restore_desk_objects
  compile_guest_shell_process_objects
  for o in $(makefile_guest_objects); do
    o="${o//$'\r'/}"
    need "$DESK/$o"
    objs+=("$DESK/$o")
  done
  objs+=("$STUB/qbfree_wayland.o" "$STUB/wl_stub_flush.o")
  collect_d3_archives

  export BFREE_GUEST_CRT0="$DESK/crt0.o"
  export BFREE_GUEST_COMPAT="$DESK/guest_link_compat.o"
  export BFREE_GUEST_SERIAL="$DESK/guest_serial.o"
  export BFREE_ROOT="$ROOT"
  export BFREE_D3_WAYLAND_LINK=1

  need "$DESK/crt0.o"
  need "$DESK/guest_serial.o"
  need "$DESK/desktop.ld"
  need "$ROOT/tools/guest_desktop_link.sh"

  local gdl_lf="/tmp/bfree-guest_desktop_link-$$.sh"
  tr -d '\r' < "$ROOT/tools/guest_desktop_link.sh" > "$gdl_lf"

  echo "[d3-desktop] guest_desktop_link (${#objs[@]} objs, ${#D3_ARCHIVES[@]} archives, libqbfree+wayland libc=${BFREE_ELF_LIBC_PATH:-auto})"
  rm -f desktop desktop.elf
  if ! bash "$gdl_lf" \
    -o "$DESK/desktop.elf" \
    "-T$DESK/desktop.ld" \
    "${objs[@]}" \
    "${D3_ARCHIVES[@]}"; then
    rm -f "$gdl_lf"
    return 1
  fi
  rm -f "$gdl_lf"
  need "$DESK/desktop.elf"
}

compile_guest_main_d3_o() {
  local mk_defines mk_cxx mk_cxxflags mk_incpath expanded_flags qpa_inc
  restore_desk_tree
  qpa_inc="$(resolve_qpa_inc)" || {
    echo "FAIL: bfree/bfree_guest_abi.h not found (need Program gui_server/integration_gui/bfree_qpa)" >&2
    echo "  expected under $ROOT/gui_server/integration_gui/bfree_qpa/bfree/" >&2
    return 1
  }
  mk_defines="$(makefile_guest_var DEFINES)"
  mk_cxx="$(makefile_guest_var CXX)"
  mk_cxxflags="$(makefile_guest_var CXXFLAGS)"
  mk_incpath="$(makefile_guest_var INCPATH)"
  if [[ -z "$mk_defines" || -z "$mk_cxx" || -z "$mk_cxxflags" ]]; then
    echo "FAIL: Makefile.guest-elf lacks CXX/DEFINES/CXXFLAGS" >&2
    return 1
  fi
  expanded_flags="${mk_cxxflags//\$(DEFINES)/$mk_defines -DBFREE_D3_WAYLAND_QPA}"
  if [[ -n "$MUSL_INC" ]]; then
    expanded_flags+=" -idirafter $MUSL_INC"
  fi
  rm -f guest_main.o
  echo "[d3-desktop] compile guest_main.o via $mk_cxx (-DBFREE_D3_WAYLAND_QPA qpa=$qpa_inc)"
  # shellcheck disable=SC2086
  $mk_cxx -c $expanded_flags $mk_incpath -I. -I"$qpa_inc" -o guest_main.o guest_main.cpp
  if ! strings guest_main.o | grep -qF '[desktop_qt] D3 wayland desk session'; then
    echo "FAIL: guest_main.o lacks D3 wayland session string (-DBFREE_D3_WAYLAND_QPA not applied?)" >&2
    return 1
  fi
}

echo "[d3-desktop] guest Qt=$BFREE_QT_GUEST_BUILD_DIR musl_inc=$MUSL_INC musl_libc=${BFREE_ELF_LIBC_PATH:-missing}"
restore_desk_tree
restore_desk_objects
bash "$ROOT/tools/ensure_guest_resource_holder_va.sh" "$DESK/guest_resource_holder_va.h" "$DESK/desktop.elf"

echo "[d3-desktop] compile guest_link_compat.o (D3 /tmp/bfree-d3-wl marker)"
rm -f guest_link_compat.o
# Force compat rebuild (main_tls must use linker symbol, not stale 0x62c9540 VA).
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o

echo "[d3-desktop] compile qbfree_wayland.o + wl_stub_flush.o"
make -C "$STUB" wl_stub_flush.o
if ! "$CXX" "${WL_CXXFLAGS[@]}" -c -o "$STUB/qbfree_wayland.o" "$STUB/qbfree_wayland.cpp"; then
  echo "FAIL: qbfree_wayland.o compile failed" >&2
  exit 1
fi

echo "[d3-desktop] recompile guest_main.o (-DBFREE_D3_WAYLAND_QPA, /tmp/bfree-d3-wl fast path)"
compile_guest_main_d3_o

echo "[d3-desktop] link desktop.elf (wayland QPA + libqbfree symbol refs)"
link_d3_desktop

if ! strings desktop.elf | grep -qF '[desktop_qt] D3 wayland desk session'; then
  echo "FAIL: desktop.elf lacks D3 wayland desk session string (guest_main.o stale?)" >&2
  exit 1
fi
if strings desktop.elf | grep -qF 'plugin bfree only'; then
  if strings desktop.elf | grep -qF 'plugin wayland only'; then
    echo "[d3-desktop] OK: D3 wayland plugin path present (bfree path also in binary for mmap fallback)"
  else
    echo "WARN: desktop.elf lacks plugin wayland only — check -DBFREE_D3_WAYLAND_QPA" >&2
  fi
fi

echo "[d3-desktop] sync guest_resource_holder_va.h"
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
rm -f guest_link_compat.o guest_main.o
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o
compile_guest_main_d3_o
link_d3_desktop
bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
holder_nm="$(nm desktop.elf 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }')"
holder_hdr="$(sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' guest_resource_holder_va.h 2>/dev/null || true)"
echo "[d3-desktop] holder nm=$holder_nm hdr=$holder_hdr"

echo "[d3-desktop] patch embedded bfree_guest_phdrs[] (vfork __copy_tls GP)"
python3 "$ROOT/tools/emit_guest_compat_phdrs.py" desktop.elf "$ROOT/tools/guest_link_compat.cpp"
# emit_guest_compat_phdrs.py updates guest_link_compat.cpp — recompile compat + relink so
# bfree_guest_ptr_in_text() reads the emitted PT_LOAD p_filesz (not a stale .o rodata copy).
echo "[d3-desktop] recompile guest_link_compat.o after emit-phdrs + relink"
rm -f guest_link_compat.o
bash "$ROOT/tools/compile_guest_link_compat.sh" guest_link_compat.o
link_d3_desktop
python3 "$ROOT/tools/patch_desktop_phdrs_embedded.py" desktop.elf
python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" desktop.elf

bash "$ROOT/tools/check_d3_desktop_main_tls.sh" desktop.elf
bash "$ROOT/tools/check_desktop_holder_embedded.sh" desktop.elf

if ! strings desktop.elf | grep -qF 'phdr-text-v1'; then
  echo "FAIL: desktop.elf lacks compat build id — guest_link_compat.o not linked?" >&2
  exit 1
fi
desk_sha="$(sha256sum desktop.elf | awk '{print $1}')"
echo "[d3-desktop] desktop.elf sha256=$desk_sha"
if [[ "$desk_sha" == "8da145f187b7d47fa08de86fc12b3c970ba45db9e72a5f970bda7f5081d0e0ff" ]]; then
  echo "FAIL: stale desktop.elf sha256 (guest_link_compat still old — git pull && rebuild)" >&2
  exit 1
fi
if [[ "$desk_sha" == "461102ad0effac05078bd9ffe3b247d41112268eab852d6712c88da1e5fd8172" ]]; then
  echo "FAIL: defer-env-v1-only desktop (cc59730) — sync to phdr-text-v1 and rebuild:" >&2
  echo "  bash tools/wsl_sync_d3_branch.sh" >&2
  exit 1
fi

strings desktop.elf | grep -F 'build=mmap96' | head -1 || true
echo "D3_DESKTOP_ELF=$DESK/desktop.elf"
echo "D3_DESKTOP_BYTES=$(wc -c < desktop.elf)"
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
echo "Full: BFREE_D3=1 BFREE_D3_FULL=1 bash tools/_d3_compositor_stub_smoke.sh"
