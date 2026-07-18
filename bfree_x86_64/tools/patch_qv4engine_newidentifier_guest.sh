#!/usr/bin/env bash
# Guest ExecutionEngine::newIdentifier — avoid Scope/ScopedString (null -> createPropertyKeyImpl PF).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

old = """Heap::String *ExecutionEngine::newIdentifier(const QString &text)
{
    Scope scope(this);
    ScopedString s(scope, memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text));
    s->toPropertyKey();
    return s->d();
}"""

new = """Heap::String *ExecutionEngine::newIdentifier(const QString &text)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::String *d = memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text);
    if (!d)
        return nullptr;
    if (identifierTable)
        identifierTable->asPropertyKey(d);
    return d;
#else
    Scope scope(this);
    ScopedString s(scope, memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text));
    s->toPropertyKey();
    return s->d();
#endif
}"""

if new in text:
    print("[patch_newidentifier] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[patch_newidentifier] ok")
else:
    broken = """    String str = *static_cast<String *>(static_cast<Value *>(Encode(d)));
    str.toPropertyKey();"""
    fixed = """    Value v = Value::fromHeapObject(d);
    if (String *s = v.stringValue())
        s->toPropertyKey();"""
    broken2 = """    Value v = Value::fromHeapObject(d);
    if (String *s = v.stringValue())
        s->toPropertyKey();"""
    fixed2 = """    if (identifierTable)
        identifierTable->asPropertyKey(d);"""
    if broken2 in text:
        path.write_text(text.replace(broken2, fixed2, 1))
        print("[patch_newidentifier] ok (direct asPropertyKey)")
    elif broken in text:
        path.write_text(text.replace(broken, fixed, 1))
        print("[patch_newidentifier] ok (fix broken cast)")
    else:
        raise SystemExit("[patch_newidentifier] anchor missing")
PY
