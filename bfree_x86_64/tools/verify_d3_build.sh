#!/usr/bin/env bash
# Fail fast if D3 compositor/desktop artifacts are wrong (run before smoke).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
KERNEL="${BFREE_STUB_KERNEL:-$ROOT/kernel/kernel.elf}"
COMP="$ROOT/userland/compositor_stub/compositor.elf"
DESK="$ROOT/userland/desktop_qt/desktop.elf"
fail=0

if [[ ! -s "$KERNEL" ]]; then
  echo "FAIL: missing $KERNEL" >&2
  fail=1
else
  strings "$KERNEL" | grep -qF 'build=d2c-vfork-3' || {
    echo "FAIL: $KERNEL lacks build=d2c-vfork-3" >&2
    fail=1
  }
  strings "$KERNEL" | grep -qF '[VFORK] immute ok' || {
    echo "FAIL: $KERNEL lacks [VFORK] immute ok" >&2
    fail=1
  }
  strings "$KERNEL" | grep -qF '[D3] desktop wayland exec' || {
    echo "FAIL: $KERNEL lacks [D3] desktop wayland exec" >&2
    echo "  git pull 後に kernel を再ビルド:" >&2
    echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
    fail=1
  }
fi

if [[ ! -s "$DESK" ]]; then
  echo "FAIL: missing $DESK" >&2
  fail=1
elif [[ "$(wc -c < "$DESK")" -lt 10000000 ]]; then
  echo "FAIL: $DESK too small (need >=10MB)" >&2
  fail=1
fi

if [[ -s "$COMP" ]]; then
  if ! strings "$COMP" | grep -qF '[D3] execve desktop.elf'; then
    echo "WARN: compositor lacks D3 path — smoke will rebuild with BFREE_D3=1"
  fi
fi

grep -qF '[D3] execve desktop.elf' "$ROOT/tools/_d3_compositor_stub_smoke.sh" 2>/dev/null || {
  echo "FAIL: stale _d3_compositor_stub_smoke.sh" >&2
  fail=1
}

if [[ "$fail" != 0 ]]; then
  exit 1
fi
echo "verify_d3_build: OK"
