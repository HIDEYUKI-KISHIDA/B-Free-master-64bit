#!/usr/bin/env bash
# Guest cross: Qt must not compile qdevicediscovery_udev (host pkg-config -I/usr/include).
set -euo pipefail
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi

QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
CM="$QT_SRC/qtbase/src/platformsupport/devicediscovery/CMakeLists.txt"
PCFG="$QT_SRC/qtbase/src/platformsupport/configure.cmake"
MARKER="$QT_SRC/qtbase/.bfree_guest_disable_udev_patched"

prepend_force_off() {
  local f="$1" label="$2"
  [[ -f "$f" ]] || return 0
  if grep -q 'bfree guest: libudev disabled' "$f" 2>/dev/null; then
    return 0
  fi
  local tmp
  tmp="$(mktemp)"
  {
    echo "# bfree guest: libudev disabled ($label)"
    echo "set(FEATURE_libudev OFF CACHE BOOL \"\" FORCE)"
    echo "set(FEATURE_libinput OFF CACHE BOOL \"\" FORCE)"
    echo "set(FEATURE_evdev OFF CACHE BOOL \"\" FORCE)"
    echo "set(QT_FEATURE_libudev FALSE)"
    cat "$f"
  } >"$tmp"
  mv "$tmp" "$f"
}

[[ -f "$CM" ]] || {
  echo "[patch-udev] WARN: missing $CM — skip" >&2
  exit 0
}

prepend_force_off "$PCFG" "platformsupport/configure.cmake"
prepend_force_off "$CM" "devicediscovery/CMakeLists.txt"

perl -i -pe '
  s/^(\s*)if\s*\(\s*QT_FEATURE_libudev\s*\)/$1if(FALSE AND QT_FEATURE_libudev) # bfree guest/;
  s/^(\s*.*qdevicediscovery_udev\.cpp.*)$/# bfree guest disabled: $1/;
' "$CM"

touch "$MARKER"
echo "[patch-udev] ok: platformsupport configure + devicediscovery — libudev off"
