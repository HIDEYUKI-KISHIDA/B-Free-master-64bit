#!/usr/bin/env bash
# Restore newClass-based changeVTable (in-place breaks shared Class_Empty).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

bad = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (eng)
        engine = eng;
    vtable = vt;
    ++numRedundantTransitions;
    return this;
#else"""

good = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    newClass->vtable = vt;
    ++newClass->numRedundantTransitions;
    return newClass;
#else"""

if good in text:
    print("[revert_vtable] newClass path already active")
elif bad in text:
    path.write_text(text.replace(bad, good, 1))
    print("[revert_vtable] ok")
else:
    raise SystemExit("[revert_vtable] anchor missing")
PY
