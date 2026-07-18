#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()
old = """    Value v = Value::fromHeapObject(d);
    if (String *s = v.stringValue())
        s->toPropertyKey();"""
new = """    if (identifierTable)
        identifierTable->asPropertyKey(d);"""
if new in t:
    print("[fix_newidentifier_direct] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[fix_newidentifier_direct] ok")
else:
    raise SystemExit("[fix_newidentifier_direct] anchor missing")
PY
