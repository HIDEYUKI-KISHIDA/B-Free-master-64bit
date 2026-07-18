#!/usr/bin/env bash
# Qt 6.8 bundled glslang: SpvBuilder.h needs <cstdint> with GCC 13+ (host + guest).
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail

QT_SRC="${BFREE_QT_SRC:-/root/src/qt6}"
HDR="$QT_SRC/qtshadertools/src/3rdparty/glslang/SPIRV/SpvBuilder.h"

if [[ ! -f "$HDR" ]]; then
  echo "[patch] missing $HDR" >&2
  exit 1
fi

if grep -q '#include <cstdint>' "$HDR"; then
  echo "[patch] ok: SpvBuilder.h already includes <cstdint>"
  exit 0
fi

python3 - "$HDR" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding='utf-8')
needle = '#include <stack>\n'
insert = '#include <stack>\n#include <cstdint>\n'
if needle not in text:
    raise SystemExit(f"[patch] anchor not found in {path}")
path.write_text(text.replace(needle, insert, 1), encoding='utf-8')
print(f"[patch] added #include <cstdint> to {path}")
PY
