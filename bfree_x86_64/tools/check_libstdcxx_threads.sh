#!/usr/bin/env bash
# Verify guest libstdc++.a includes threading (required for Qt Core link).
set -eu
LIB="${1:-${BFREE_ELF_LIBSTDCXX_PATH:-${BFREE_ELF_GCC_ROOT:-/root/bfree-native-build/x86_64-elf-gcc-full}/lib/libstdc++.a}}"
! test -f "$LIB" && echo "[FAIL] missing: $LIB" >&2 && exit 1
echo "[check] $LIB"
SZ=$(stat -c%s "$LIB" 2>/dev/null || wc -c <"$LIB")
echo "[check] size=$SZ bytes"
# Freestanding (--disable-hosted-libstdcxx) libstdc++ is ~1.2MB and has no condition_variable.
if nm -C "$LIB" 2>/dev/null | grep -qE 'std::condition_variable|__gthread_mutex_t'; then
  echo "[OK] libstdc++ has threading (condition_variable / gthread)"
  exit 0
fi
if nm "$LIB" 2>/dev/null | grep -qE 'condition_variable|__gthread_'; then
  echo "[OK] libstdc++ has threading symbols"
  exit 0
fi
echo "[FAIL] libstdc++ looks freestanding (no threads) — Qt guest link will fail." >&2
echo "  Reconfigure with hosted libstdc++:" >&2
echo "    rm -rf \$BFREE_LINUX_BUILD_ROOT/gcc-build/x86_64-elf/libstdc++-v3" >&2
echo "    bash tools/configure_libstdcxx_manual.sh" >&2
echo "    cd \$BFREE_LINUX_BUILD_ROOT/gcc-build && make -j\$(nproc) all-target-libstdc++-v3 install-target-libstdc++-v3" >&2
exit 1
