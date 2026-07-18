#!/usr/bin/env bash
# Strip CRLF from repo shell scripts (Windows /mnt/c checkout).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
n=0
while IFS= read -r -d '' f; do
  if grep -q $'\r' "$f" 2>/dev/null; then
    sed -i 's/\r$//' "$f"
    echo "fixed: $f"
    n=$((n + 1))
  fi
done < <(find "$ROOT" -maxdepth 1 -name '*.sh' -print0; find "$ROOT/tools" "$ROOT/third_party" -name '*.sh' -print0 2>/dev/null)
echo "[fix_scripts_lf] $n file(s) updated"
