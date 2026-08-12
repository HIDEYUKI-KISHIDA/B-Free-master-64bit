#!/usr/bin/env bash
# Phase B — compositor-first ISO: compositor.elf PID1 + Wayland desktop client.
#
# Prereqs (on developer machine — not all are in git):
#   gui_server/          → compositor.elf (make -C gui_server)
#   userland/init/       → init.elf (login → exec desktop)
#   build.sh             → ISO assembly (or use stage + grub-mkrescue below)
#
# Usage (WSL, from Program/bfree_x86_64):
#   export BFREE_QT_GUEST_BUILD_DIR=/home/h_kis/out/bfree-qt6-guest-static
#   bash tools/build_compositor_guest_iso.sh
#   bash tools/build_compositor_guest_iso.sh run   # QEMU smoke
set -euo pipefail
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
RUN_QEMU=0
[[ "${1:-}" == "run" ]] && RUN_QEMU=1

export BFREE_QT_GUEST_BUILD_DIR="${BFREE_QT_GUEST_BUILD_DIR:-$HOME/out/bfree-qt6-guest-static}"
export BFREE_MVP_GUEST_QML=1
export BFREE_ISO_DESKTOP_SHELL=1
export BFREE_AUTO_LOGIN=1
export BFREE_QT_GUEST_LINKED=1

echo "=== B-Free compositor-first ISO ==="

if [[ ! -d "$ROOT/gui_server" ]]; then
  echo "[FAIL] missing $ROOT/gui_server (Wayland compositor sources)." >&2
  echo "  Copy from full tree: C:\\Users\\h_kis\\Desktop\\B-Free-master\\Program\\bfree_x86_64\\gui_server" >&2
  exit 1
fi

BFREE_ROOT="$ROOT" bash <(sed 's/\r$//' "$ROOT/tools/ensure_x86_64_elf_toolchain.sh")

echo "=== 1/6 kernel (BFREE_BOOT_GUI_FIRST=1) ==="
make -C kernel compositor-kernel

echo "=== 2/6 compositor.elf ==="
make -C gui_server clean
make -C gui_server
COMP="$ROOT/gui_server/compositor.elf"
[[ -f "$COMP" ]] || COMP="$ROOT/gui_server/build/compositor.elf"
if [[ ! -f "$COMP" ]]; then
  echo "[FAIL] compositor.elf not found after gui_server build" >&2
  exit 1
fi
mkdir -p iso_root/boot
cp -f "$COMP" iso_root/boot/compositor.elf

echo "=== 3/6 init.elf ==="
if [[ ! -d userland/init ]]; then
  echo "[FAIL] missing userland/init — copy from full developer tree." >&2
  exit 1
fi
make -C userland/init clean
BFREE_AUTO_LOGIN=1 BFREE_COMPOSITOR_BOOT=1 make -C userland/init all
cp -f userland/init/init.elf iso_root/boot/init.elf

echo "=== 4/6 desktop.elf (Wayland client) ==="
if bash tools/build_guest_desktop_wayland_elf.sh; then
  cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf
else
  echo "[WARN] Wayland desktop link failed — falling back to bfree QPA desktop.elf" >&2
  bash tools/build_guest_desktop_elf.sh || true
  cp -f userland/desktop_qt/desktop.elf iso_root/boot/desktop.elf 2>/dev/null || true
fi

echo "=== 5/6 stage QML + grub ==="
bash tools/stage_mvp_qml.sh
bash tools/stage_iso_grub_template.sh
if [[ -f userland/busybox_guest/busybox.elf ]]; then
  cp -f userland/busybox_guest/busybox.elf iso_root/boot/busybox.elf
fi

echo "=== 6/6 ISO ==="
if [[ -x "$ROOT/build.sh" ]]; then
  bash "$ROOT/build.sh"
else
  if ! command -v grub-mkrescue >/dev/null 2>&1; then
    echo "[FAIL] grub-mkrescue missing (apt install grub-pc-bin xorriso)" >&2
    exit 1
  fi
  grub-mkrescue -o bfree.iso iso_root -- -volid BFREE
fi

ISO="$ROOT/bfree.iso"
ls -la "$ISO"
echo ""
echo "Boot: GRUB default = compositor + Wayland desktop"
echo "Serial markers: [BOOT] Compositor handoff | [desktop_qt] platform=wayland"

if [[ "$RUN_QEMU" == "1" ]]; then
  exec qemu-system-x86_64 -cdrom "$ISO" -m 1024M -vga std -serial mon:stdio -display gtk -no-reboot
fi
