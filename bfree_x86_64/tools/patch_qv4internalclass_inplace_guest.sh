#!/usr/bin/env bash
# Keep proto in-place + slim init(other); never touch changeVTable (use revert script).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()
changed = False

proto_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    if (prototype == proto)
        return this;
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    if (proto)
        proto->setUsedAsProto();
    newClass->prototype = proto;
    ++newClass->numRedundantTransitions;
    return newClass;
#else"""

proto_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (eng)
        engine = eng;
    if (prototype == proto)
        return this;
    if (proto)
        proto->setUsedAsProto();
    prototype = proto;
    ++numRedundantTransitions;
    return this;
#else"""

init_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    Base::init();
    ExecutionEngine *eng = other->engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    engine = eng;
    new (&propertyTable) PropertyHash();
    new (&nameMap) SharedInternalClassData<PropertyKey>(eng);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(eng);
    new (&transitions) QVarLengthArray<Transition, 1>();
    vtable = other->vtable;
    prototype = other->prototype;
    parent = other;
    size = other->size;
    numRedundantTransitions = other->numRedundantTransitions;
    flags = other->flags;
    protoId = eng ? eng->newProtoId() : other->protoId;
    if (eng)
        internalClass.set(eng, other->internalClass);
    return;
#endif"""

init_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    Base::init();
    ExecutionEngine *eng = other->engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    engine = eng;
    new (&propertyTable) PropertyHash();
    if (eng) {
        new (&nameMap) SharedInternalClassData<PropertyKey>(eng);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(eng);
    } else {
        new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    }
    new (&transitions) QVarLengthArray<Transition, 1>();
    vtable = other->vtable;
    prototype = other->prototype;
    parent = other;
    size = other->size;
    numRedundantTransitions = other->numRedundantTransitions;
    flags = other->flags;
    protoId = other->protoId;
    return;
#endif"""

for old, new, tag in [
    (proto_old, proto_new, "changePrototype in-place"),
    (init_old, init_new, "init(other) slim"),
]:
    if new in text:
        print(f"[patch_inplace] {tag} already applied")
    elif old in text:
        text = text.replace(old, new, 1)
        changed = True
        print(f"[patch_inplace] ok ({tag})")
    else:
        print(f"[patch_inplace] skip {tag}")

if changed:
    path.write_text(text)
print("[patch_inplace] done (vtable untouched)")
PY
