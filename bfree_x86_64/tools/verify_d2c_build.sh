#!/usr/bin/env bash
# Fail fast if D2c vfork branch/build artifacts are wrong (run before smoke).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
EXPECT="${BFREE_D2C_EXPECT:-d799e1a}"
KERNEL="${BFREE_STUB_KERNEL:-$ROOT/kernel/kernel.elf}"
HELLO="$ROOT/userland/compositor_stub/qt_wl_hello.elf"
fail=0

cd "$ROOT/.."
HEAD="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
BR="$(git branch --show-current 2>/dev/null || echo unknown)"
echo "git branch=$BR HEAD=$HEAD (want ${EXPECT}*)"
if [[ "$BR" != "cursor/d2c-vfile-slots-9760" ]]; then
  echo "FAIL: wrong branch ($BR) — run: bash bfree_x86_64/tools/wsl_checkout_d2c_vfork.sh" >&2
  fail=1
fi
if [[ "$HEAD" != "$EXPECT"* ]]; then
  echo "FAIL: HEAD $HEAD is not commit $EXPECT*" >&2
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
fi

if [[ -s "$HELLO" ]]; then
  sz="$(wc -c < "$HELLO")"
  if [[ "$sz" -gt 1000000 ]]; then
    strings "$HELLO" | grep -qF 'build=d2c-vfork-3' || {
      echo "FAIL: qt_wl_hello.elf lacks build=d2c-vfork-3 (rebuild hello)" >&2
      fail=1
    }
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
