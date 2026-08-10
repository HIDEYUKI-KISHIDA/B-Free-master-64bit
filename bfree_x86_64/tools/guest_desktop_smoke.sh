#!/usr/bin/env bash
if [[ -z "${BFREE_FIX_CRLF_DONE:-}" ]] && grep -q $'\r' "$0" 2>/dev/null; then
  export BFREE_FIX_CRLF_DONE=1
  exec bash <(sed 's/\r$//' "$0") "$@"
fi
# Headless Qt guest desktop smoke: build (optional), AUTO_LOGIN, QEMU, serial pass/fail.
#
#   export PATH=/root/x86_64-elf-toolchain/bin:$PATH
#   cd Program/bfree_x86_64
#   bash tools/guest_desktop_smoke.sh
#
# Env:
#   BFREE_SMOKE_REBUILD=1     rebuild desktop.elf + init (AUTO_LOGIN) + ISO
#   BFREE_SMOKE_TIMEOUT=300    seconds (default 300)
#   BFREE_SMOKE_GUI=1         use -display gtk (WSLg); default -display none
#   GUEST_DESKTOP_SUCCESS_LINE  override success grep (default: QML ready)
#
# Exit: 0 = success marker, 1 = crash/exception, 2 = timeout, 3 = build erro
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

for _tb in /root/x86_64-elf-toolchain/bin "${HOME}/x86_64-elf-toolchain/bin" /usr/local/x86_64-elf/bin; do
  if [[ -x "$_tb/x86_64-elf-g++" ]]; then
    export PATH="$_tb:$PATH"
    break
  fi
done

SERIAL="$(mktemp "${TMPDIR:-/tmp}/bfree-guest-smoke.XXXXXX")"
TIMEOUT_SEC="${BFREE_SMOKE_TIMEOUT:-180}"
POLL_MS="${BFREE_SMOKE_POLL_MS:-150}"
SUCCESS_LINE="${GUEST_DESKTOP_SUCCESS_LINE:-\[desktop_qt\] QML ready, entering event loop}"
ISO="$ROOT/bfree.iso"
ELF="$ROOT/userland/desktop_qt/desktop.elf"
QEMU_PID=""

# Kill leftover QEMU from prior runs (WSL often accumulates these).
pkill -f 'qemu-system-x86_64.*bfree\.iso' 2>/dev/null || true
sleep 0.2

cleanup() {
  if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "[smoke] serial log: $SERIAL"
echo "[smoke] timeout: ${TIMEOUT_SEC}s  success: $SUCCESS_LINE"

if [[ "${BFREE_SMOKE_REBUILD:-0}" == "1" ]]; then
  echo "[smoke] rebuild desktop.elf..."
  bash "$ROOT/tools/build_guest_desktop_elf.sh"
  echo "[smoke] rebuild init.elf (BFREE_AUTO_LOGIN=1)..."
  make -C "$ROOT/userland/init" clean
  BFREE_AUTO_LOGIN=1 make -C "$ROOT/userland/init" all
  echo "[smoke] ISO..."
  export BFREE_QT_GUEST_LINKED=1 BFREE_ISO_DESKTOP_SHELL=1 BFREE_MVP_GUEST_QML=1
  export BFREE_AUTO_LOGIN=1
  bash "$ROOT/build.sh"
else
  if [[ ! -f "$ISO" ]]; then
    echo "[smoke] no $ISO — set BFREE_SMOKE_REBUILD=1" >&2
    exit 3
  fi
fi

: >"$SERIAL"

DISPLAY_OPTS=(-display none -vga std)
if [[ "${BFREE_SMOKE_GUI:-0}" == "1" ]]; then
  if [[ -z "${DISPLAY:-}" && -d /mnt/wslg ]]; then
    export DISPLAY=:0
  fi
  if [[ -n "${DISPLAY:-}" ]] && qemu-system-x86_64 -display help 2>/dev/null | grep -q gtk; then
    DISPLAY_OPTS=(-display gtk -vga std)
    echo "[smoke] GUI mode (DISPLAY=${DISPLAY:-set})"
  else
    echo "[smoke] BFREE_SMOKE_GUI=1 but no DISPLAY — serial-only" >&2
  fi
fi

CPU_OPTS=(-cpu "${BFREE_QEMU_CPU:-qemu64,+rdrand,+rdseed}")
echo "[smoke] QEMU start (cpu=${CPU_OPTS[*]})..."
qemu-system-x86_64 \
  -cdrom "$ISO" \
  -m "${BFREE_QEMU_MEM:-512M}" \
  -no-reboot \
  "${CPU_OPTS[@]}" \
  "${DISPLAY_OPTS[@]}" \
  -serial "file:$SERIAL" \
  -monitor none &
QEMU_PID=$!

deadline=$((SECONDS + TIMEOUT_SEC))
status=2
poll_s="$(awk "BEGIN {printf \"%.3f\", ${POLL_MS}/1000}")"
while (( SECONDS < deadline )); do
  if ! kill -0 "$QEMU_PID" 2>/dev/null; then
    wait "$QEMU_PID" || true
    QEMU_PID=""
    if grep -qE "$SUCCESS_LINE" "$SERIAL" 2>/dev/null; then
      status=0
    elif grep -qE '\[EXCEPTION\]|\[PANIC\]' "$SERIAL" 2>/dev/null; then
      status=1
    else
      status=2
    fi
    break
  fi
  if grep -qE "$SUCCESS_LINE" "$SERIAL" 2>/dev/null; then
    echo "[smoke] PASS: $SUCCESS_LINE"
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
    QEMU_PID=""
    status=0
    break
  fi
  if grep -qE '\[EXCEPTION\]|\[PANIC\]' "$SERIAL" 2>/dev/null; then
    echo "[smoke] FAIL: guest fault (see symbolize below)"
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
    QEMU_PID=""
    status=1
    break
  fi
  sleep "$poll_s"
done

if [[ "$status" == 2 ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
  echo "[smoke] TIMEOUT after ${TIMEOUT_SEC}s"
fi

echo "========== serial tail =========="
tail -n 40 "$SERIAL" || true
echo "================================="

if [[ "$status" == 1 ]]; then
  if [[ "${BFREE_SMOKE_NO_SYMBOLIZE:-0}" != "1" ]]; then
    bash "$ROOT/tools/symbolize_guest_crash.sh" "$SERIAL" || true
  else
    echo "[smoke] symbolize skipped (BFREE_SMOKE_NO_SYMBOLIZE=1)"
  fi
elif [[ "$status" == 2 ]]; then
  if [[ "${BFREE_SMOKE_NO_SYMBOLIZE:-0}" != "1" ]]; then
    bash "$ROOT/tools/symbolize_guest_crash.sh" "$SERIAL" 2>/dev/null || true
  fi
  echo "[smoke] hint: partial progress markers above; increase BFREE_SMOKE_TIMEOUT or BFREE_SMOKE_REBUILD=1"
fi

echo "[smoke] log kept: $SERIAL"

# Product-path ENOSYS gate (①): unique [ENOSYS] nr must be 0 on this boot log.
ENOSYS_UNIQUE=$(grep -aoE '\[ENOSYS\] nr=[0-9a-fA-Fx]+' "$SERIAL" 2>/dev/null \
  | sed 's/.*nr=//' | sort -u | wc -l | tr -d ' ' || true)
ENOSYS_UNIQUE=${ENOSYS_UNIQUE:-0}
if [[ "$ENOSYS_UNIQUE" -eq 0 ]]; then
  echo "DESKTOP_ENOSYS_RESULT: PASS unique=0 (guest_desktop_smoke)"
else
  echo "DESKTOP_ENOSYS_RESULT: FAIL unique=${ENOSYS_UNIQUE} (guest_desktop_smoke)"
  grep -aoE '\[ENOSYS\] nr=[0-9a-fA-Fx]+' "$SERIAL" 2>/dev/null \
    | sed 's/.*nr=//' | sort | uniq -c | sort -rn | head -40 || true
  if [[ "$status" -eq 0 ]]; then
    status=1
  fi
fi

trap - EXIT INT TERM
exit "$status"
