from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
idt = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4identifiertable.cpp")

# 1) newIdentifier — PropertyKey without IdentifierTable hash (fromStringOrSymbol only)
et = eng.read_text()
nid_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::String *d = memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text);
    if (!d)
        return nullptr;
    return d;"""
nid_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::String *d = memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text);
    if (!d)
        return nullptr;
    d->identifier = PropertyKey::fromStringOrSymbol(this, d);
    return d;"""
if nid_new not in et:
    if nid_old not in et:
        raise SystemExit("[v203] newIdentifier anchor missing")
    et = et.replace(nid_old, nid_new, 1)
    print("[v203] newIdentifier fromStringOrSymbol id")
else:
    print("[v203] newIdentifier already patched")

# 2) guestBuiltinStringKey — use jsStrings slots (avoid IdentifierTable QString path)
key_old = """static PropertyKey guestBuiltinStringKey(ExecutionEngine *e, const QString &text)
{
    if (e->identifierTable)
        return e->identifierTable->asPropertyKey(text);
    return PropertyKey::invalid();
}"""
key_new = """static PropertyKey guestBuiltinStringKey(ExecutionEngine *e, const QString &text)
{
    if (!e)
        return PropertyKey::invalid();
    struct Map { const char *lit; int slot; };
    static const Map kMap[] = {
        {"length", ExecutionEngine::String_length}, {"prototype", ExecutionEngine::String_prototype},
        {"constructor", ExecutionEngine::String_constructor}, {"name", ExecutionEngine::String_name},
        {"callee", ExecutionEngine::String_callee}, {"lastIndex", ExecutionEngine::String_lastIndex},
        {"index", ExecutionEngine::String_index}, {"input", ExecutionEngine::String_input},
        {"toString", ExecutionEngine::String_toString}, {"toLocaleString", ExecutionEngine::String_toLocaleString},
        {"valueOf", ExecutionEngine::String_valueOf},
    };
    for (const Map &m : kMap) {
        if (text == QLatin1String(m.lit)) {
            const Value v = e->jsStrings[m.slot];
            if (const String *s = v.as<String>())
                return s->propertyKey();
        }
    }
    if (e->identifierTable)
        return e->identifierTable->asPropertyKey(text);
    return PropertyKey::invalid();
}"""
if key_new not in et:
    if key_old not in et:
        raise SystemExit("[v203] guestBuiltinStringKey anchor missing")
    et = et.replace(key_old, key_new, 1)
    print("[v203] guestBuiltinStringKey jsStrings lookup")
else:
    print("[v203] guestBuiltinStringKey already patched")

# 3) ArrayProto direct assign (like number_proto)
et = et.replace(
    "jsObjects[ArrayProto] = Value::fromHeapObject(arrayProto);",
    "jsObjects[ArrayProto] = arrayProto;",
    1,
)

eng.write_text(et)

# 4) resolveStringEntry null guard
it = idt.read_text()
loop_old = """    while (Heap::StringOrSymbol *e = entriesByHash[idx]) {
        if (e->stringHash == hash && e->toQString() == s)
            return static_cast<Heap::String *>(e);"""
loop_new = """    while (Heap::StringOrSymbol *e = entriesByHash[idx]) {
        if (!e)
            break;
        if (e->stringHash == hash && e->toQString() == s)
            return static_cast<Heap::String *>(e);"""
if loop_new not in it:
    if loop_old not in it:
        print("[v203] resolveStringEntry anchor missing (skipped)")
    else:
        idt.write_text(it.replace(loop_old, loop_new, 1))
        print("[v203] resolveStringEntry null guard")
else:
    print("[v203] resolveStringEntry already patched")

print("[v203] done")
