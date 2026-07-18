#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = p.read_text()
start = text.find("Heap::InternalClass *InternalClass::asProtoClass()")
end = text.find("\n}\n\nstatic void updateProtoUsage", start)
if start < 0 or end < 0:
    raise SystemExit("asProtoClass block not found")
stub = '''Heap::InternalClass *InternalClass::asProtoClass()
{
    if (!this)
        return nullptr;
    if (isUsedAsProto())
        return this;

#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (eng)
        engine = eng;
    flags |= UsedAsProto;
    return this;
#else
    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::ProtoClass };
    Transition &t = lookupOrInsertTransition(temp);
    if (t.lookup)
        return t.lookup;

    Scope scope(engine);
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    newClass->flags |= UsedAsProto;

    t.lookup = newClass;
    Q_ASSERT(t.lookup);
    return newClass;
#endif
}'''
text = text[:start] + stub + text[end+2:]
p.write_text(text)
print("[fix_asproto] ok (in-place guest)")
PY
