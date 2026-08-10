#!/usr/bin/env bash
# Boot Qt desktop.elf on current posix-holes kernel; collect [ENOSYS] histogram.
# Gate (product path ①):
#   - must reach exec_initrd transfer to desktop.elf
#   - unique [ENOSYS] nr count must be 0
# Exit: 0 = DESKTOP_ENOSYS_RESULT PASS, 1 = FAIL (ENOSYS or no transfer), 2 = build/stage error
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="${HOME}/x86_64-elf-toolchain/bin:/usr/bin:/bin:${PATH:-}"

OUT_DIR="${BFREE_DESKTOP_ENOSYS_OUT:-$ROOT/out/desktop_enosys}"
mkdir -p "$OUT_DIR"
ELF_SRC=userland/desktop_qt/desktop.elf
SZ=$(stat -c%s "$ELF_SRC")
if [[ "$SZ" -lt 100000 ]]; then
  echo "ERROR: Qt desktop.elf missing/stub ($SZ)" >&2
  exit 2
fi

echo "[1] rebuild init: AUTO_LOGIN -> desktop.elf (not busybox)"
make -C userland/init clean >/dev/null
make -C userland/init BFREE_AUTO_LOGIN=1 2>&1 | tail -5
strings userland/init/init.elf | grep -E 'AUTO_LOGIN|busybox.elf|desktop.elf' | head -10

echo "[2] stage ISO (kernel + Qt desktop + init)"
ISO_STAGE="${BFREE_DESKTOP_ENOSYS_STAGE:-/tmp/bfree-desktop-enosys-iso}"
ISO="${BFREE_DESKTOP_ENOSYS_ISO:-/tmp/bfree-desktop-enosys.iso}"
QLOG="$OUT_DIR/qemu.log"
NOLOG="$OUT_DIR/serial.nolog"
HIST="$OUT_DIR/enosys.hist"
RESULT="$OUT_DIR/result.txt"
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
install -m 0644 kernel/kernel.elf "$ISO_STAGE/boot/kernel.elf"
install -m 0644 userland/init/init.elf "$ISO_STAGE/boot/initrd.img"
install -m 0644 "$ELF_SRC" "$ISO_STAGE/boot/desktop.elf"
cp -f iso_root/boot/grub/grub.cfg "$ISO_STAGE/boot/grub/grub.cfg"
sed -i 's/^set default=.*/set default=0/' "$ISO_STAGE/boot/grub/grub.cfg"
sed -i '/busybox.elf/d;/p8test.elf/d' "$ISO_STAGE/boot/grub/grub.cfg" || true

echo "[3] grub-mkrescue (desktop.elf=${SZ} bytes)..."
grub-mkrescue -o "$ISO" "$ISO_STAGE" -- -volid BFREE >"$OUT_DIR/mkrescue.log" 2>&1
ls -la "$ISO"

TIMEOUT_SEC="${BFREE_DESKTOP_ENOSYS_TIMEOUT:-220}"
echo "[4] QEMU boot (serial only, ~${TIMEOUT_SEC}s)..."
: > "$QLOG"
(
  sleep $((TIMEOUT_SEC - 20))
  printf '\n'
  sleep 2
) | timeout "$TIMEOUT_SEC" qemu-system-x86_64 -m 1024M -no-reboot -cdrom "$ISO" \
    -display none -serial mon:stdio >>"$QLOG" 2>&1 || true

tr -d '\r' <"$QLOG" >"$NOLOG"
echo "=== boot markers ==="
grep -aE 'AUTO_LOGIN|exec_initrd|desktop_qt|QML ready|ENOSYS|Page Fault|PANIC|FATAL|load.*desktop|ELF|transfer to' "$NOLOG" | head -80 || true

echo "=== ENOSYS histogram (unique nr) ==="
if grep -aoE '\[ENOSYS\] nr=[0-9a-fA-Fx]+' "$NOLOG" >/dev/null 2>&1; then
  grep -aoE '\[ENOSYS\] nr=[0-9a-fA-Fx]+' "$NOLOG" \
    | sed 's/.*nr=//' | sort | uniq -c | sort -rn | head -50 \
    | tee "$HIST"
else
  echo "(none)" | tee "$HIST"
fi

UNIQUE=$(grep -aoE '\[ENOSYS\] nr=[0-9a-fA-Fx]+' "$NOLOG" 2>/dev/null \
  | sed 's/.*nr=//' | sort -u | wc -l | tr -d ' ')
TRANSFER=0
if grep -aqE 'transfer to desktop\.elf|exec_initrd: loaded entry' "$NOLOG"; then
  TRANSFER=1
fi
QML=0
if grep -aqF '[desktop_qt] QML ready' "$NOLOG"; then
  QML=1
fi

echo "=== unique count === $UNIQUE"
echo "=== transfer_desktop === $TRANSFER"
echo "=== qml_ready === $QML"

PASS=1
REASON=""
if [[ "$TRANSFER" -ne 1 ]]; then
  PASS=0
  REASON="no_desktop_transfer"
fi
if [[ "$UNIQUE" -ne 0 ]]; then
  PASS=0
  if [[ -n "$REASON" ]]; then
    REASON="${REASON}+enosys_unique_${UNIQUE}"
  else
    REASON="enosys_unique_${UNIQUE}"
  fi
fi

if [[ "$PASS" -eq 1 ]]; then
  LINE="DESKTOP_ENOSYS_RESULT: PASS unique=0 transfer=1 qml_ready=${QML}"
  echo "$LINE" | tee "$RESULT"
  echo DONE
  exit 0
fi

LINE="DESKTOP_ENOSYS_RESULT: FAIL unique=${UNIQUE} transfer=${TRANSFER} qml_ready=${QML} reason=${REASON}"
echo "$LINE" | tee "$RESULT"
echo "FAIL: see $HIST and $NOLOG" >&2
echo DONE
exit 1
