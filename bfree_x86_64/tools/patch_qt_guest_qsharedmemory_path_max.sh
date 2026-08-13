#!/usr/bin/env bash
# Guest nostdinc++: qsharedmemory.cpp defines MAX_PATH from PATH_MAX without limits.h.
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
f="$QT_SRC/qtbase/src/corelib/ipc/qsharedmemory.cpp"
marker="$QT_SRC/qtbase/.bfree_guest_qsharedmemory_path_max_patched"

[[ -f "$f" ]] || { echo "[patch] missing: $f" >&2; exit 1; }

# Qt sources on /mnt/c may be CRLF; normalize so perl anchors match.
if grep -q $'\r' "$f" 2>/dev/null; then
  sed -i 's/\r$//' "$f"
fi

already_ok() {
  grep -qE '#[[:space:]]*define[[:space:]]+MAX_PATH[[:space:]]+4096' "$f" \
    && ! grep -qE '#[[:space:]]*define[[:space:]]+MAX_PATH[[:space:]]+PATH_MAX' "$f"
}

if [[ -f "$marker" ]] && already_ok; then
  echo "[patch] ok: qsharedmemory MAX_PATH already patched"
  exit 0
fi

# Drop stale marker when an earlier run only added limits.h or failed to rewrite MAX_PATH.
rm -f "$marker"

perl -0pi -e '
  s/#ifndef MAX_PATH\s*\n#\s*define MAX_PATH PATH_MAX\s*\n#endif\s*\n/
#ifndef MAX_PATH\n# define MAX_PATH 4096\n#endif\n/s;
  s/#ifndef MAX_PATH\s*\n#\s*ifdef PATH_MAX\s*\n#\s*define MAX_PATH PATH_MAX\s*\n#\s*else\s*\n#\s*define MAX_PATH \d+\s*\n#\s*endif\s*\n#endif\s*\n/
#ifndef MAX_PATH\n# define MAX_PATH 4096\n#endif\n/s;
' "$f"

if ! already_ok; then
  echo "[patch] ERROR: failed to rewrite MAX_PATH in $f" >&2
  grep -n 'MAX_PATH\|PATH_MAX\|limits.h' "$f" | head -20 >&2 || true
  exit 1
fi

touch "$marker"
echo "[patch] ok: qsharedmemory MAX_PATH=4096 (no PATH_MAX dependency)"
