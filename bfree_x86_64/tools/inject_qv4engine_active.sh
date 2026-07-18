#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = p.read_text()
if "bfree_guest_qv4_set_active_engine(this)" in text:
    print("[inject] set_active_engine already present")
else:
    old = '    bfree_guest_qv4_heartbeat("ctor");\n    if (m_engineId == 1) {'
    new = '    bfree_guest_qv4_set_active_engine(this);\n    bfree_guest_qv4_heartbeat("ctor");\n    if (m_engineId == 1) {'
    if old not in text:
        raise SystemExit("[inject] ctor anchor missing")
    p.write_text(text.replace(old, new, 1))
    print("[inject] ok set_active_engine")
PY
