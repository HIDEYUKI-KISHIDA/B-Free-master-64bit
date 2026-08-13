#!/usr/bin/env bash
# Guest cross: Qt must not compile qdevicediscovery_udev (host pkg-config -I/usr/include).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
CM="$QT_SRC/qtbase/src/platformsupport/devicediscovery/CMakeLists.txt"
MARKER="$QT_SRC/qtbase/.bfree_guest_disable_udev_patched"

[[ -f "$CM" ]] || {
  echo "[patch-udev] WARN: missing $CM — skip" >&2
  exit 0
}

if [[ -f "$MARKER" ]]; then
  if grep -q 'bfree guest: libudev disabled' "$CM" 2>/dev/null; then
    echo "[patch-udev] ok: already patched"
    exit 0
  fi
fi

# Qt 6.x: disable libudev backend at CMake parse time (before target_sources).
if ! grep -q 'bfree guest: libudev disabled' "$CM"; then
  tmp="$(mktemp)"
  {
    echo '# bfree guest: libudev disabled (host /usr/include leak under -nostdinc)'
    echo 'set(QT_FEATURE_libudev FALSE)'
    echo 'set(FEATURE_libudev OFF CACHE BOOL "" FORCE)'
    cat "$CM"
  } >"$tmp"
  mv "$tmp" "$CM"
fi

# Belt-and-suspenders: neuter any remaining udev condition / source line.
perl -i -pe '
  s/^(\s*)if\s*\(\s*QT_FEATURE_libudev\s*\)/$1if(FALSE AND QT_FEATURE_libudev) # bfree guest/;
  s/^(\s*.*qdevicediscovery_udev\.cpp.*)$/# bfree guest disabled: $1/;
' "$CM"

touch "$MARKER"
echo "[patch-udev] ok: devicediscovery CMakeLists — libudev backend off"
