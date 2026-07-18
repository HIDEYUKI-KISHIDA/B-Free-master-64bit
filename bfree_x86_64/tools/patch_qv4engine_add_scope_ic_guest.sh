#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()
old = '    bfree_guest_qv4_heartbeat("pre_jsstrings");\n#else\n    ic = newInternalClass'
new = '    bfree_guest_qv4_heartbeat("pre_jsstrings");\n    Scope scope(this);\n    Scoped<InternalClass> ic(scope);\n#else\n    ic = newInternalClass'
if 'pre_jsstrings");\n    Scope scope(this);' in t:
    print("[add_scope_ic] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[add_scope_ic] ok")
else:
    raise SystemExit("[add_scope_ic] anchor missing")
PY
