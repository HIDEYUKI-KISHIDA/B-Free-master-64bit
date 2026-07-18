from pathlib import Path

mm = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
idt = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4identifiertable.cpp")

# 1) allocWithStringData null guards
mmt = mm.read_text()
alloc_old = """    typename ManagedType::Data *allocWithStringData(std::size_t unmanagedSize, Arg1 &&arg1)
    {
        typename ManagedType::Data *o = reinterpret_cast<typename ManagedType::Data *>(allocString(unmanagedSize));
        o->internalClass.set(engine, ManagedType::defaultInternalClass(engine));
        Q_ASSERT(o->internalClass && o->internalClass->vtable);
        o->init(std::forward<Arg1>(arg1));
        return o;
    }"""
alloc_new = """    typename ManagedType::Data *allocWithStringData(std::size_t unmanagedSize, Arg1 &&arg1)
    {
        typename ManagedType::Data *o = reinterpret_cast<typename ManagedType::Data *>(allocString(unmanagedSize));
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!o)
            return nullptr;
        Heap::InternalClass *ic = ManagedType::defaultInternalClass(engine);
        if (!ic)
            return nullptr;
        o->internalClass.set(engine, ic);
#else
        o->internalClass.set(engine, ManagedType::defaultInternalClass(engine));
#endif
        Q_ASSERT(o->internalClass && o->internalClass->vtable);
        o->init(std::forward<Arg1>(arg1));
        return o;
    }"""
if alloc_new not in mmt:
    if alloc_old not in mmt:
        raise SystemExit("[v201] allocWithStringData anchor missing")
    mm.write_text(mmt.replace(alloc_old, alloc_new, 1))
    print("[v201] allocWithStringData guest guards")
else:
    print("[v201] allocWithStringData already patched")

# 2) newIdentifier: skip asPropertyKey during ctor (defer to avoid IdentifierTable PF)
et = eng.read_text()
nid_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::String *d = memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text);
    if (!d)
        return nullptr;
    if (identifierTable)
        identifierTable->asPropertyKey(d);
    return d;"""
nid_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::String *d = memoryManager->allocWithStringData<String>(text.size() * sizeof(QChar), text);
    if (!d)
        return nullptr;
    return d;"""
if nid_new not in et:
    if nid_old not in et:
        raise SystemExit("[v201] newIdentifier anchor missing")
    et = et.replace(nid_old, nid_new, 1)
    print("[v201] newIdentifier skip asPropertyKey")
else:
    print("[v201] newIdentifier already patched")

eng.write_text(et)

# 3) asPropertyKeyImpl: null-guard hash chain entries
it = idt.read_text()
loop_old = """    while (Heap::StringOrSymbol *e = entriesByHash[idx]) {
        if (e->stringHash == hash && e->toQString() == str->toQString()) {"""
loop_new = """    while (Heap::StringOrSymbol *e = entriesByHash[idx]) {
        if (!e)
            break;
        if (e->stringHash == hash && e->toQString() == str->toQString()) {"""
if loop_new not in it:
    if loop_old not in it:
        print("[v201] asPropertyKeyImpl loop anchor missing (skipped)")
    else:
        it = it.replace(loop_old, loop_new, 1)
        idt.write_text(it)
        print("[v201] asPropertyKeyImpl null guard")
else:
    print("[v201] asPropertyKeyImpl already patched")

print("[v201] done")
