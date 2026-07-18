#!/usr/bin/env bash
# Guest Symbol::create — allocManaged with Class_Symbol (internalClass set before init).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4symbol.cpp")
text = path.read_text()

inc = '#include <qv4engine_p.h>\n'
if inc not in text:
    text = text.replace("#include <qv4identifiertable_p.h>\n",
                        "#include <qv4identifiertable_p.h>\n" + inc, 1)

old = """Heap::Symbol *Symbol::create(ExecutionEngine *e, const QString &s)
{
    Q_ASSERT(s.at(0) == QLatin1Char('@'));
    return e->memoryManager->alloc<Symbol>(s);
}"""

new = """Heap::Symbol *Symbol::create(ExecutionEngine *e, const QString &s)
{
    Q_ASSERT(s.at(0) == QLatin1Char('@'));
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *ic = e->internalClasses(EngineBase::Class_Symbol);
    if (!ic)
        return nullptr;
    Heap::Symbol *d = e->memoryManager->allocManaged<Symbol>(ic);
    if (!d)
        return nullptr;
    d->init(s);
    return d;
#else
    return e->memoryManager->alloc<Symbol>(s);
#endif
}"""

if new in text:
    print("[patch_symbol_create] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[patch_symbol_create] ok")
else:
    raise SystemExit("[patch_symbol_create] anchor missing")
PY
