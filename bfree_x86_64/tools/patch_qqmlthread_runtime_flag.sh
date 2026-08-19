#!/usr/bin/env bash
# Patch stock QQmlThread::isThisThread() to honor the post-loop runtime flag.
# Does NOT make isThisThread always true. STAGE 5 qrc stays on the stock body.
#
#   bash tools/patch_qqmlthread_runtime_flag.sh
#   bash tools/force_rebuild_stock_qqmlthread.sh

set -euo pipefail

if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi

QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
SRC="$QT_SRC/qtdeclarative/src/qml/qml/ftw/qqmlthread.cpp"

if [[ ! -f "$SRC" ]]; then
  echo "[FAIL] missing $SRC" >&2
  exit 1
fi
if grep -q 'bfree_isThisThread_always_true' "$SRC"; then
  echo "[FAIL] $SRC still has always-true isThisThread" >&2
  exit 1
fi
if grep -q 'bfree_guest_typeloader_main_ok' "$SRC"; then
  echo "[ok] $SRC already has runtime flag"
  exit 0
fi
if ! grep -q 'return d->isCurrentThread();' "$SRC"; then
  echo "[FAIL] $SRC isThisThread is not stock" >&2
  exit 1
fi

python3 - "$SRC" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
t = p.read_text(encoding="utf-8")
old = """bool QQmlThread::isThisThread() const
{
    return d->isCurrentThread();
}"""
new = """extern "C" int bfree_guest_typeloader_main_ok(void);

bool QQmlThread::isThisThread() const
{
    if (bfree_guest_typeloader_main_ok())
        return true;
    return d->isCurrentThread();
}"""
if old not in t:
    raise SystemExit("stock isThisThread body not found")
p.write_text(t.replace(old, new, 1), encoding="utf-8")
print("[ok] patched", p)
PY
