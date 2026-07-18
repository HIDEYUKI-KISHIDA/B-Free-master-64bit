#!/usr/bin/env bash
# Guest: PropertyKey via identifierTable (jsStrings Value slots may be empty on stack).
set -eu
python3 - <<'PY'
from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static PropertyKey guestBuiltinStringKey(ExecutionEngine *e, const QString &text)
{
    if (e->identifierTable)
        return e->identifierTable->asPropertyKey(text);
    return PropertyKey::invalid();
}
static PropertyKey guestBuiltinSymbolKey(ExecutionEngine *e, int symIndex)
{
    Symbol *s = reinterpret_cast<Symbol *>(e->jsSymbols + symIndex);
    if (s) {
        if (Heap::Symbol *d = s->d())
            return d->identifier;
    }
    return PropertyKey::invalid();
}
#endif

"""

marker = "ExecutionEngine::ExecutionEngine(QJSEngine *jsEngine)"
if "guestBuiltinStringKey" not in t:
    t = t.replace(marker, helper + marker, 1)

replacements = [
    ("id_length()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("length"))'),
    ("id_prototype()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("prototype"))'),
    ("id_constructor()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("constructor"))'),
    ("id_name()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("name"))'),
    ("id_lastIndex()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("lastIndex"))'),
    ("id_index()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("index"))'),
    ("id_input()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("input"))'),
    ("id_callee()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("callee"))'),
    ("id_toString()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("toString"))'),
    ("id_toLocaleString()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("toLocaleString"))'),
    ("id_valueOf()->propertyKey()", 'guestBuiltinStringKey(this, QStringLiteral("valueOf"))'),
    ("symbol_iterator()->propertyKey()", "guestBuiltinSymbolKey(this, Symbol_iterator)"),
    ("symbol_hasInstance()->propertyKey()", "guestBuiltinSymbolKey(this, Symbol_hasInstance)"),
]

count = 0
for old, new in replacements:
    if old in t:
        t = t.replace(old, new)
        count += 1

if count == 0 and "guestBuiltinStringKey(this" in t:
    print("[guest_propertykey] already applied")
elif count:
    p.write_text(t)
    print(f"[guest_propertykey] ok ({count} patterns)")
else:
    raise SystemExit("[guest_propertykey] anchor missing")
PY
