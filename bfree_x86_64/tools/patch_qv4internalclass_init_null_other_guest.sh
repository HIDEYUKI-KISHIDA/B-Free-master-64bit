#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

old = """void InternalClass::init(Heap::InternalClass *other)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Base::init();
    ExecutionEngine *eng = other->engine;"""

new = """void InternalClass::init(Heap::InternalClass *other)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!other) {
        ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
        if (eng)
            init(eng);
        return;
    }
    Base::init();
    ExecutionEngine *eng = other->engine;"""

if new in t:
    print("[init_null_other] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[init_null_other] ok")
else:
    raise SystemExit("[init_null_other] anchor missing")
PY
