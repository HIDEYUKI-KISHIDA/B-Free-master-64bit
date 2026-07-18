#!/usr/bin/env bash
# Parse serial log for [EXCEPTION] RIP=... and addr2line against desktop.elf.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${GUEST_DESKTOP_ELF:-$ROOT/userland/desktop_qt/desktop.elf}"
LOG="${1:-}"

if [[ -z "$LOG" || ! -f "$LOG" ]]; then
  echo "usage: GUEST_DESKTOP_ELF=.../desktop.elf bash tools/symbolize_guest_crash.sh /path/to/serial.log" >&2
  exit 2
fi
if [[ ! -f "$ELF" ]]; then
  echo "[symbolize] missing ELF: $ELF" >&2
  exit 2
fi

for _tb in /root/x86_64-elf-toolchain/bin "${HOME}/x86_64-elf-toolchain/bin" /usr/local/x86_64-elf/bin; do
  if [[ -x "$_tb/x86_64-elf-addr2line" ]]; then
    export PATH="$_tb:$PATH"
    break
  fi
done

ADDR2LINE="$(command -v x86_64-elf-addr2line 2>/dev/null || true)"
if [[ -z "$ADDR2LINE" ]]; then
  echo "[symbolize] x86_64-elf-addr2line not found" >&2
  exit 2
fi

RIP="$(grep -oE 'RIP=[0-9A-Fa-f]+' "$LOG" | tail -1 | cut -d= -f2 || true)"
CR2="$(grep -oE 'CR2=[0-9A-Fa-f]+' "$LOG" | tail -1 | cut -d= -f2 || true)"

if [[ -z "$RIP" ]]; then
  echo "[symbolize] no [EXCEPTION] RIP= in $LOG"
  exit 1
fi

echo "[symbolize] ELF=$ELF"
echo "[symbolize] CR2=${CR2:-?}  RIP=$RIP"
"$ADDR2LINE" -f -e "$ELF" "0x$RIP" || true

# Last desktop_qt milestone before fault
echo "--- serial milestones (last hits) ---"
for m in \
  '\[desktop_qt\] QML ready' \
  '\[desktop_qt\] QGuiApplication OK' \
  '\[desktop_qt\] setenv done' \
  '\[desktop_qt\] main entry' \
  '\[H\]' \
  '\[F\]'; do
  if grep -qE "$m" "$LOG" 2>/dev/null; then
    echo "  OK  $m"
  fi
done
