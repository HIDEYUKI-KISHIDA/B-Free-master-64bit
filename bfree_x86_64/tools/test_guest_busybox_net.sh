#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f "$ROOT/bfree.iso" ]]; then
  echo "Run: ENABLE_RUNTIME_NET=1 BFREE_GUEST_BUSYBOX=1 bash build.sh" >&2
  exit 1
fi

pkill -9 -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
sleep 1

ISO="$ROOT/bfree.iso"
echo "[test_guest_busybox_net] Testing Network in B-Free Guest"
echo "Run the following commands in the guest:"
echo "  ifconfig"
echo "  ping -c 3 10.0.2.2"
echo "  nc -u -l -p 8080"
echo "(Ctrl-A x to quit QEMU)"

exec qemu-system-x86_64 \
  -m 512M \
  -no-reboot \
  -cdrom "$ISO" \
  -display none \
  -serial mon:stdio \
  -net nic,model=e1000 -net user,hostfwd=udp::8080-:8080
