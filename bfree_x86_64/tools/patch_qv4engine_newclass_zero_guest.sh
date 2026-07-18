#!/usr/bin/env bash
# Zero InternalClass from allocIC before init (garbage nameMap.d skips guest init).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

old_empty = """    memset(classes, 0, sizeof(classes));
    classes[Class_Empty] = memoryManager->allocIC<InternalClass>();
    classes[Class_Empty]->init(this);"""

new_empty = """    memset(classes, 0, sizeof(classes));
    classes[Class_Empty] = memoryManager->allocIC<InternalClass>();
#if defined(BFREE_GUEST_FIXED_STACK)
    std::memset(classes[Class_Empty], 0, sizeof(Heap::InternalClass));
#endif
    classes[Class_Empty]->init(this);"""

old_newclass = """Heap::InternalClass *ExecutionEngine::newClass(Heap::InternalClass *other)
{
    Heap::InternalClass *ic = memoryManager->allocIC<InternalClass>();
    ic->init(other);
    return ic;
}"""

new_newclass = """Heap::InternalClass *ExecutionEngine::newClass(Heap::InternalClass *other)
{
    Heap::InternalClass *ic = memoryManager->allocIC<InternalClass>();
#if defined(BFREE_GUEST_FIXED_STACK)
    std::memset(ic, 0, sizeof(Heap::InternalClass));
#endif
    ic->init(other);
    return ic;
}"""

if new_empty in t:
    print("[newclass_zero] class_empty already patched")
elif old_empty in t:
    t = t.replace(old_empty, new_empty, 1)
    print("[newclass_zero] ok (class_empty)")
else:
    raise SystemExit("[newclass_zero] class_empty anchor missing")

if new_newclass in t:
    print("[newclass_zero] newClass already patched")
elif old_newclass in t:
    t = t.replace(old_newclass, new_newclass, 1)
    print("[newclass_zero] ok (newClass)")
else:
    raise SystemExit("[newclass_zero] newClass anchor missing")

if "#include <cstring>" not in t and "std::memset" in t:
    t = t.replace("#include <private/qv4", "#include <cstring>\n#include <private/qv4", 1)
    print("[newclass_zero] ok (cstring)")

p.write_text(t)
print("[newclass_zero] done")
PY
