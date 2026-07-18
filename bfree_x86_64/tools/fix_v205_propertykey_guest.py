from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
sym = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4symbol.cpp")

# guestBuiltinStringKey — read identifier from jsStrings heap, never IdentifierTable
et = eng.read_text()
key_old = """static PropertyKey guestBuiltinStringKey(ExecutionEngine *e, const QString &text)
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
            const Value &v = e->jsStrings[m.slot];
            if (v.isManaged()) {
                if (Heap::String *d = static_cast<Heap::String *>(v.m())) {
                    if (d->identifier.isValid())
                        return d->identifier;
                    return PropertyKey::fromStringOrSymbol(e, d);
                }
            }
        }
    }
    return PropertyKey::invalid();
}"""
if key_new not in et:
    if key_old not in et:
        raise SystemExit("[v205] guestBuiltinStringKey anchor missing")
    et = et.replace(key_old, key_new, 1)
    print("[v205] guestBuiltinStringKey heap id only")
else:
    print("[v205] guestBuiltinStringKey already patched")

# heartbeats after protos_skip for FunctionPrototype block
if 'bfree_guest_qv4_heartbeat("func_proto_ic")' not in et:
    et = et.replace(
        '    bfree_guest_qv4_heartbeat("protos_skip_post_root");\n#else',
        '    bfree_guest_qv4_heartbeat("protos_skip_post_root");\n    bfree_guest_qv4_heartbeat("func_proto_enter");\n#else',
        1,
    )
    print("[v205] func_proto_enter heartbeat")

eng.write_text(et)

# Symbol::init — restore identifier via fromStringOrSymbol (no IdentifierTable)
st = sym.read_text()
sym_old = """    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);
#else
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
    identifier = PropertyKey::fromStringOrSymbol(internalClass->engine, this);
#endif"""
sym_new = """    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);
#else
    QString desc(s);
    StringOrSymbol::init(desc.data_ptr());
    identifier = PropertyKey::fromStringOrSymbol(internalClass->engine, this);
#endif"""
# v202 removed the if (eng) block - find current state
if "/* defer PropertyKey during guest ctor */" in st:
    st = st.replace("    /* defer PropertyKey during guest ctor */", """    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);""", 1)
    sym.write_text(st)
    print("[v205] Symbol::init identifier restored")
else:
    print("[v205] Symbol::init already ok")

print("[v205] done")
