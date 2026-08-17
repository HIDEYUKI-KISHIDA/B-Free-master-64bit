#!/usr/bin/env bash
# After any desktop relink: rewrite holder VA and rebuild until nm == header.
# Never deletes desktop.elf.good-running. Refuses a stub-sized desktop.
# Does not rebuild Qml. Run from bfree_x86_64.
set -eu
if [ -d userland/desktop_qt ]; then
  ROOT="$(pwd)"
elif [ -d "$(dirname "$0")/../userland/desktop_qt" ]; then
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
else
  echo "[converge] run from bfree_x86_64 (userland/desktop_qt missing)" >&2
  exit 1
fi
DESK="$ROOT/userland/desktop_qt"
MIN_QT_ELF=10000000

holder_nm() {
  nm "$1" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }'
}

hdr_va() {
  sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$DESK/guest_resource_holder_va.h"
}

elf_ok() {
  local f="$1"
  if [ ! -f "$f" ]; then
    echo "[converge] missing $f" >&2
    return 1
  fi
  local sz
  sz="$(stat -c%s "$f")"
  if [ "$sz" -lt "$MIN_QT_ELF" ]; then
    echo "[converge] stub-sized $f ($sz). restore desktop.elf.good-running" >&2
    return 1
  fi
  return 0
}

ELF="$DESK/desktop"
if [ ! -f "$ELF" ]; then
  ELF="$DESK/desktop.elf"
fi
elf_ok "$ELF" || exit 1

i=0
while [ "$i" -lt 6 ]; do
  i=$((i + 1))
  bash "$ROOT/tools/update_guest_resource_holder_va.sh" "$ELF" "$DESK/guest_resource_holder_va.h"
  nmv="$(holder_nm "$ELF")"
  hdr="$(hdr_va)"
  echo "[converge] pass=$i hdr=$hdr nm=$nmv size=$(stat -c%s "$ELF")"
  if [ -n "$hdr" ] && [ "$hdr" = "$nmv" ]; then
    echo "[ok] holder converged $hdr"
    exit 0
  fi
  rm -f "$DESK/guest_main.o" "$DESK/guest_link_compat.o"
  if [ -f "$DESK/Makefile.bfree" ]; then
    make -C "$DESK" -f Makefile.bfree guest_link_compat.o
  else
    echo "[converge] Makefile.bfree missing; rebuild guest_link_compat.o yourself" >&2
    exit 1
  fi
  make -C "$DESK" -f Makefile.guest-elf guest_main.o
  make -C "$DESK" -f Makefile.guest-elf desktop
  ELF="$DESK/desktop"
  elf_ok "$ELF" || exit 1
done
echo "[err] holder did not converge" >&2
exit 1
