#!/usr/bin/env bash
# Patch libstdc++-v3/configure for x86_64-elf + musl.
set -eu

LINUX_ROOT="${BFREE_LINUX_BUILD_ROOT:-$HOME/bfree-native-build}"
SRC="${BFREE_GCC_SRC_ROOT:-$LINUX_ROOT/gcc-src}/gcc-${BFREE_GCC_VERSION:-13.2.0}"
CFG="$SRC/libstdc++-v3/configure"
BAK="$CFG.bak-bfree"

[[ -f "$CFG" ]] || { echo "missing $CFG" >&2; exit 1; }
[[ -f "$BAK" ]] || cp -a "$CFG" "$BAK"

cp -a "$BAK" "$CFG"

sed -i '/Link tests are not allowed after GCC_NO_EXECUTABLES/ {
  s/.*as_fn_error.*/  : # bfree: skip link tests/
}' "$CFG"
sed -i '/No support for this host\/target combination/ {
  s/.*as_fn_error.*/  : # bfree: allow elf+mudl/
}' "$CFG"

python3 - "$CFG" <<'PY'
import sys
path = sys.argv[1]
lines = open(path, encoding="utf-8", errors="replace").read().splitlines()

needle = "checking whether the C compiler works"
start = next((i for i, L in enumerate(lines) if needle in L), None)
if start is None:
    print("compiler-works anchor missing", file=sys.stderr)
    sys.exit(1)
n = 0
for j in range(start, min(start + 130, len(lines))):
    L = lines[j]
    if "ac_cv_c_compiler_works=no" in L:
        lines[j] = L.replace("ac_cv_c_compiler_works=no", "ac_cv_c_compiler_works=yes # bfree")
        n += 1
    if "./conftest" in L or "conftest)" in L:
        if "test" in L or "exec" in L or "val=" in L:
            lines[j] = "  true # bfree: skip run conftest on build host"
            n += 1
print(f"[patch-libstdc++] C compiler works block: {n} line(s) patched")

# Only set the cache variable in the output-filetype block (do NOT replace as_fn_error — breaks configure).
needle2 = "checking output filetype"
start2 = next((i for i, L in enumerate(lines) if needle2 in L), None)
m = 0
if start2 is not None:
    for j in range(start2, min(start2 + 25, len(lines))):
        L = lines[j]
        if "libstdcxx_cv_output_filetype=" in L and "bfree" not in L:
            lines[j] = "libstdcxx_cv_output_filetype=elf64-x86-64 # bfree"
            m += 1
            break
    print(f"[patch-libstdc++] output filetype block: {m} line(s) patched")

open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
PY

sed -i 's/ac_cv_c_compiler_works=no/ac_cv_c_compiler_works=yes # bfree/g' "$CFG" || true

if ! bash -n "$CFG" 2>/dev/null; then
  echo "[patch-libstdc++] ERROR: configure syntax broken — restoring $BAK" >&2
  cp -a "$BAK" "$CFG"
  exit 1
fi
echo "[patch-libstdc++] OK (bash -n passed)"
