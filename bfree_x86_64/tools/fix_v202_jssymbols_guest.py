from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
sym = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4symbol.cpp")

# 1) Symbol::init — skip PropertyKey registration during guest ctor (IdentifierTable PF)
st = sym.read_text()
init_old = """    if (eng)
        identifier = PropertyKey::fromStringOrSymbol(eng, this);
#else"""
init_new = """    /* defer PropertyKey during guest ctor */
#else"""
if init_new not in st:
    if init_old not in st:
        raise SystemExit("[v202] Symbol::init anchor missing")
    sym.write_text(st.replace(init_old, init_new, 1))
    print("[v202] Symbol::init skip PropertyKey")
else:
    print("[v202] Symbol::init already patched")

# 2) jsSymbols — null-safe Value assign (nullptr -> Value::fromHeapObject PF)
et = eng.read_text()
anchor = '    bfree_guest_qv4_heartbeat("post_jsstrings");\n\n    jsSymbols[Symbol_hasInstance]'
macro = """    bfree_guest_qv4_heartbeat("post_jsstrings");
#if defined(BFREE_GUEST_FIXED_STACK)
#define GUEST_SET_JSSYMBOL(slot, text) do { \\
        if (Heap::Symbol *_sy = Symbol::create(this, text)) \\
            jsSymbols[slot] = _sy; \\
    } while (0)
#endif

    jsSymbols[Symbol_hasInstance]"""

if "GUEST_SET_JSSYMBOL" not in et:
    if anchor not in et:
        raise SystemExit("[v202] jsSymbols anchor missing")
    et = et.replace(anchor, macro, 1)

pairs = [
    ('jsSymbols[Symbol_hasInstance] = Symbol::create(this, QStringLiteral("@Symbol.hasInstance"));',
     'GUEST_SET_JSSYMBOL(Symbol_hasInstance, QStringLiteral("@Symbol.hasInstance"));'),
    ('jsSymbols[Symbol_isConcatSpreadable] = Symbol::create(this, QStringLiteral("@Symbol.isConcatSpreadable"));',
     'GUEST_SET_JSSYMBOL(Symbol_isConcatSpreadable, QStringLiteral("@Symbol.isConcatSpreadable"));'),
    ('jsSymbols[Symbol_iterator] = Symbol::create(this, QStringLiteral("@Symbol.iterator"));',
     'GUEST_SET_JSSYMBOL(Symbol_iterator, QStringLiteral("@Symbol.iterator"));'),
    ('jsSymbols[Symbol_match] = Symbol::create(this, QStringLiteral("@Symbol.match"));',
     'GUEST_SET_JSSYMBOL(Symbol_match, QStringLiteral("@Symbol.match"));'),
    ('jsSymbols[Symbol_replace] = Symbol::create(this, QStringLiteral("@Symbol.replace"));',
     'GUEST_SET_JSSYMBOL(Symbol_replace, QStringLiteral("@Symbol.replace"));'),
    ('jsSymbols[Symbol_search] = Symbol::create(this, QStringLiteral("@Symbol.search"));',
     'GUEST_SET_JSSYMBOL(Symbol_search, QStringLiteral("@Symbol.search"));'),
    ('jsSymbols[Symbol_species] = Symbol::create(this, QStringLiteral("@Symbol.species"));',
     'GUEST_SET_JSSYMBOL(Symbol_species, QStringLiteral("@Symbol.species"));'),
    ('jsSymbols[Symbol_split] = Symbol::create(this, QStringLiteral("@Symbol.split"));',
     'GUEST_SET_JSSYMBOL(Symbol_split, QStringLiteral("@Symbol.split"));'),
    ('jsSymbols[Symbol_toPrimitive] = Symbol::create(this, QStringLiteral("@Symbol.toPrimitive"));',
     'GUEST_SET_JSSYMBOL(Symbol_toPrimitive, QStringLiteral("@Symbol.toPrimitive"));'),
    ('jsSymbols[Symbol_toStringTag] = Symbol::create(this, QStringLiteral("@Symbol.toStringTag"));',
     'GUEST_SET_JSSYMBOL(Symbol_toStringTag, QStringLiteral("@Symbol.toStringTag"));'),
    ('jsSymbols[Symbol_unscopables] = Symbol::create(this, QStringLiteral("@Symbol.unscopables"));',
     'GUEST_SET_JSSYMBOL(Symbol_unscopables, QStringLiteral("@Symbol.unscopables"));'),
    ('jsSymbols[Symbol_revokableProxy] = Symbol::create(this, QStringLiteral("@Proxy.revokableProxy"));',
     'GUEST_SET_JSSYMBOL(Symbol_revokableProxy, QStringLiteral("@Proxy.revokableProxy"));'),
]
for old, new in pairs:
    et = et.replace(old, new)
eng.write_text(et)
print("[v202] jsSymbols null-safe macro")
