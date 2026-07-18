#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

old = """Heap::InternalClass *ExecutionEngine::newClass(Heap::InternalClass *other)
{
    Heap::InternalClass *ic = memoryManager->allocIC<InternalClass>();
#if defined(BFREE_GUEST_FIXED_STACK)
    std::memset(ic, 0, sizeof(Heap::InternalClass));
#endif
    ic->init(other);
    return ic;
}"""

new = """Heap::InternalClass *ExecutionEngine::newClass(Heap::InternalClass *other)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!other)
        return nullptr;
#endif
    Heap::InternalClass *ic = memoryManager->allocIC<InternalClass>();
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!ic)
        return nullptr;
    std::memset(ic, 0, sizeof(Heap::InternalClass));
#endif
    ic->init(other);
    return ic;
}"""

if new in t:
    print("[newclass_null] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[newclass_null] ok")
else:
    raise SystemExit("[newclass_null] anchor missing")
PY
