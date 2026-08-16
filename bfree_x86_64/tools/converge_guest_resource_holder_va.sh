#!/usr/bin/env bash
# After any desktop relink: rewrite holder VA and rebuild until nm == header.
# Does not rebuild Qml. Run from bfree_x86_64.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
ELF="$DESK/desktop"
if [ ! -f "$ELF" ] && [ -f "$DESK/desktop.elf" ]; then
  ELF="$DESK/desktop.elf"
fi
if [ ! -f "$ELF" ]; then
  echo "[converge] missing $DESK/desktop" >&2
  exit 1
fi

holder_nm() {
  nm "$1" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }'
}

hdr_va() {
  sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$DESK/guest_resource_holder_va.h"
}

i=0
while [ "$i" -lt 6 ]; do
  i=$((i + 1))
  bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$ELF" "$DESK/guest_resource_holder_va.h"
  nmv="$(holder_nm "$ELF")"
  hdr="$(hdr_va)"
  echo "[converge] pass=$i hdr=$hdr nm=$nmv"
  if [ -n "$hdr" ] && [ "$hdr" = "$nmv" ]; then
    echo "[ok] holder converged $hdr"
    exit 0
  fi
  rm -f "$DESK/guest_main.o" "$DESK/guest_link_compat.o" "$DESK/desktop" "$DESK/desktop.elf"
  if [ -f "$DESK/Makefile.bfree" ]; then
    make -C "$DESK" -f Makefile.bfree guest_link_compat.o
  else
    echo "[converge] Makefile.bfree missing; rebuild guest_link_compat.o yourself" >&2
    exit 1
  fi
  make -C "$DESK" -f Makefile.guest-elf guest_main.o
  make -C "$DESK" -f Makefile.guest-elf desktop
  ELF="$DESK/desktop"
done
echo "[err] holder did not converge" >&2
exit 1
