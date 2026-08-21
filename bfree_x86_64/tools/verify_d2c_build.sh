#!/usr/bin/env bash
# Fail fast if D2c vfork branch/build artifacts are wrong (run before smoke).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MIN_COMMIT="${BFREE_D2C_MIN:-0c457f5}"
KERNEL="${BFREE_STUB_KERNEL:-$ROOT/kernel/kernel.elf}"
HELLO="$ROOT/userland/compositor_stub/qt_wl_hello.elf"
fail=0

cd "$ROOT/.."
HEAD="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
BR="$(git branch --show-current 2>/dev/null || echo unknown)"
echo "git branch=$BR HEAD=$HEAD (need branch cursor/d2c-vfile-slots-9760, ancestor of $MIN_COMMIT)"
if [[ "$BR" != "cursor/d2c-vfile-slots-9760" ]]; then
  echo "FAIL: wrong branch ($BR) — run checkout from Program/:" >&2
  echo "  curl -fsSL 'https://raw.githubusercontent.com/HIDEYUKI-KISHIDA/B-Free-master-64bit/cursor/d2c-vfile-slots-9760/bfree_x86_64/tools/wsl_checkout_d2c_vfork.sh' | bash" >&2
  fail=1
fi
if ! git merge-base --is-ancestor "$MIN_COMMIT" HEAD 2>/dev/null; then
  echo "FAIL: HEAD $HEAD is not descended from $MIN_COMMIT (git fetch して checkout し直してください)" >&2
  fail=1
fi

cd "$ROOT"
if [[ ! -s "$KERNEL" ]]; then
  echo "FAIL: missing $KERNEL" >&2
  fail=1
else
  strings "$KERNEL" | grep -qF 'build=d2c-vfork-3' || {
    echo "FAIL: $KERNEL lacks build=d2c-vfork-3 (rebuild kernel on d2c branch)" >&2
    fail=1
  }
  strings "$KERNEL" | grep -qF '[VFORK] eg' || {
    echo "FAIL: $KERNEL lacks [VFORK] eg marker" >&2
    fail=1
  }
  strings "$KERNEL" | grep -qF '[VFORK] immute ok' || {
    echo "FAIL: $KERNEL lacks [VFORK] immute ok — run: make -C kernel clean && make -C kernel RELEASE=1" >&2
    fail=1
  }
fi

if [[ -s "$HELLO" ]]; then
  sz="$(wc -c < "$HELLO")"
  if [[ "$sz" -gt 1000000 ]]; then
    if ! strings "$HELLO" | grep -qF 'build=d2c-vfork-3'; then
      echo "WARN: qt_wl_hello.elf lacks build=d2c-vfork-3 (copied hybrid-qpa OK for vfork smoke)"
    fi
  fi
fi

grep -qF 'ISO kernel OK' "$ROOT/tools/_d2c_compositor_stub_smoke.sh" 2>/dev/null || {
  echo "FAIL: smoke script is stale (missing ISO cmp check)" >&2
  fail=1
}

if [[ "$fail" != 0 ]]; then
  exit 1
fi
echo "verify_d2c_build: OK"
