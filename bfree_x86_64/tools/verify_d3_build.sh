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
  strings "$KERNEL" | grep -qF '[TLS] scrub tail bss ok' || {
    echo "FAIL: $KERNEL lacks [TLS] scrub tail bss (WSL vfork Qt/musl BSS fix)" >&2
    echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
    fail=1
  }
  GOOD_KSHA="$ROOT/tools/kernel.d3.good.sha256"
  if [[ -f "$GOOD_KSHA" ]]; then
    expect_k="$(grep -E '^[0-9a-f]{64}$' "$GOOD_KSHA" | head -1)"
    if [[ -n "$expect_k" ]]; then
      actual_k="$(sha256sum "$KERNEL" | awk '{print $1}')"
      if [[ "$actual_k" != "$expect_k" ]]; then
        echo "FAIL: kernel sha256 mismatch (stale build — TLS bootstrap may be missing)" >&2
        echo "  actual  $actual_k" >&2
        echo "  expect  $expect_k" >&2
        echo "  make -C kernel clean && make -C kernel RELEASE=1" >&2
        fail=1
      fi
    fi
  fi
fi

if [[ ! -s "$DESK" ]]; then
  echo "FAIL: missing $DESK" >&2
  fail=1
elif [[ "$(wc -c < "$DESK")" -lt 10000000 ]]; then
  echo "FAIL: $DESK too small (need >=10MB)" >&2
  fail=1
else
  HDR="$ROOT/userland/desktop_qt/guest_resource_holder_va.h"
  holder_nm="$(nm "$DESK" 2>/dev/null | awk '/resourceGlobalData/ && /instanceEvE6holder$/ && !/_ZGV/ { print "0x" $1; exit }')"
  if [[ -x "$ROOT/tools/check_desktop_holder_embedded.sh" ]]; then
    bash "$ROOT/tools/check_desktop_holder_embedded.sh" "$DESK" || fail=1
  elif [[ -f "$HDR" ]]; then
    holder_hdr="$(sed -n 's/.*HOLDER_VA \([0-9a-fxA-FX]*\)u.*/\1/p' "$HDR")"
    if [[ -n "$holder_nm" && -n "$holder_hdr" && "$holder_nm" != "$holder_hdr" ]]; then
      echo "FAIL: holder VA mismatch nm=$holder_nm hdr=$holder_hdr (GP on vfork exec)" >&2
      echo "  bash tools/restore_desktop_good_for_d3.sh" >&2
      fail=1
    fi
  elif [[ -n "$holder_nm" ]]; then
    echo "WARN: $HDR missing (run restore or build_desktop_d3_wayland after relink)"
  fi
  if [[ -f "$ROOT/tools/check_desktop_phdrs_embedded.py" ]]; then
    python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" "$DESK" || fail=1
  fi
  if [[ "${BFREE_D3_FULL:-0}" == "1" ]]; then
    strings "$DESK" | grep -qF '[desktop_qt] D3 wayland desk session' || {
      echo "FAIL: $DESK lacks D3 wayland session (run build_desktop_d3_wayland.sh)" >&2
      fail=1
    }
    if strings "$DESK" | grep -qF 'plugin bfree only'; then
      echo "WARN: $DESK still has bfree-only plugin path — FULL smoke will fail" >&2
    fi
  fi
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
