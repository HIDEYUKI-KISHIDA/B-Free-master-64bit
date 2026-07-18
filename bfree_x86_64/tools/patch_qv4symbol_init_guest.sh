#!/usr/bin/env bash
# Guest Symbol::init: ensure internalClass + Symbol subtype before PropertyKey.
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4symbol.cpp")
text = path.read_text()

inc = '#include <qv4engine_p.h>\n'
if inc not in text:
    text = text.replace("#include <qv4identifiertable_p.h>\n",
                        "#include <qv4identifiertable_p.h>\n" + inc, 1)

decl = 'extern "C" void *bfree_guest_qv4_active_engine(void);\n'
if decl not in text:
    text = text.replace("using namespace QV4;\n", "using namespace QV4;\n" + decl, 1)

old_plain = """void Heap::Symbol::init(const QString &s)
{
    Q_ASSERT(s.at(0) == QLatin1Char('@'));
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
    identifier = PropertyKey::fromStringOrSymbol(internalClass->engine, this);
}"""

old_v1 = """void Heap::Symbol::init(const QString &s)
{
    Q_ASSERT(s.at(0) == QLatin1Char('@'));
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = internalClass ? internalClass->engine : nullptr;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);
#else
    identifier = PropertyKey::fromStringOrSymbol(internalClass->engine, this);
#endif
}"""

new = """void Heap::Symbol::init(const QString &s)
{
    Q_ASSERT(s.at(0) == QLatin1Char('@'));
#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng && internalClass)
        eng = internalClass->engine;
    if (eng && !internalClass.get()) {
        Heap::InternalClass *ic = eng->internalClasses(EngineBase::Class_Symbol);
        if (ic)
            internalClass.set(eng, ic);
    }
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
    subtype = Heap::String::StringType_Symbol;
    if (!eng && internalClass)
        eng = internalClass->engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);
#else
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
    identifier = PropertyKey::fromStringOrSymbol(internalClass->engine, this);
#endif
}"""

if new in text:
    print("[patch_qv4symbol_init] already applied")
elif old_v1 in text:
    path.write_text(text.replace(old_v1, new, 1))
    print("[patch_qv4symbol_init] ok (v2)")
elif old_plain in text:
    path.write_text(text.replace(old_plain, new, 1))
    print("[patch_qv4symbol_init] ok")
else:
    raise SystemExit("[patch_qv4symbol_init] anchor missing")
PY
