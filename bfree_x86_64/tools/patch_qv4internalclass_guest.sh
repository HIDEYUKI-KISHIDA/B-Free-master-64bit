#!/usr/bin/env bash
# Guest QV4 InternalClass: skip transition cache + safe VLA resize (avoid null ptr append).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

decl = 'extern "C" void *bfree_guest_qv4_active_engine(void);\n'
if decl not in text:
    text = text.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + decl, 1)

lookup_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
    if (transitions.capacity() == 0)
        new (&transitions) QVarLengthArray<Transition, 1>();
    for (qsizetype i = 0; i < transitions.size(); ++i) {
        if (transitions.at(i) == t)
            return transitions[i];
    }
    const qsizetype n = transitions.size();
    transitions.resize(n + 1);
    transitions[n] = t;
    return transitions[n];
#else"""

lookup_tail = """    QVarLengthArray<Transition, 1>::iterator it = std::lower_bound(transitions.begin(), transitions.end(), t);
    if (it != transitions.end() && *it == t) {
        return *it;
    } else {
        it = transitions.insert(it, t);
        return *it;
    }
#endif"""

lookup_anchor = """InternalClassTransition &InternalClass::lookupOrInsertTransition(const InternalClassTransition &t)
{
    QVarLengthArray<Transition, 1>::iterator it = std::lower_bound(transitions.begin(), transitions.end(), t);
    if (it != transitions.end() && *it == t) {
        return *it;
    } else {
        it = transitions.insert(it, t);
        return *it;
    }
}"""

lookup_stub = """InternalClassTransition &InternalClass::lookupOrInsertTransition(const InternalClassTransition &t)
{
""" + lookup_guest + """
""" + lookup_tail + "\n}"""

vtable_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    newClass->vtable = vt;
    ++newClass->numRedundantTransitions;
    return newClass;
#else"""

vtable_host = """    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::VTableChange };
    temp.vtable = vt;

    Transition &t = lookupOrInsertTransition(temp);
    if (t.lookup)
        return t.lookup;

    // create a new class and add it to the tree
    Scope scope(engine);
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    newClass->vtable = vt;

    t.lookup = newClass;
    Q_ASSERT(t.lookup);
    Q_ASSERT(newClass->vtable);
    return vtable == QV4::InternalClass::staticVTable()
            ? newClass
            : cleanInternalClass(newClass);
#endif"""

vtable_broken = """#if defined(BFREE_GUEST_FIXED_STACK)
    Scope scope(engine);
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    newClass->vtable = vt;
    return vtable == QV4::InternalClass::staticVTable()
            ? newClass
            : cleanInternalClass(newClass);
#else"""

vtable_anchor = """Heap::InternalClass *InternalClass::changeVTableImpl(const VTable *vt)
{
    Q_ASSERT(vtable != vt);

    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::VTableChange };
    temp.vtable = vt;

    Transition &t = lookupOrInsertTransition(temp);
    if (t.lookup)
        return t.lookup;

    // create a new class and add it to the tree
    Scope scope(engine);
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    newClass->vtable = vt;

    t.lookup = newClass;
    Q_ASSERT(t.lookup);
    Q_ASSERT(newClass->vtable);
    return vtable == QV4::InternalClass::staticVTable()
            ? newClass
            : cleanInternalClass(newClass);
}"""

vtable_stub = """Heap::InternalClass *InternalClass::changeVTableImpl(const VTable *vt)
{
    Q_ASSERT(vtable != vt);

""" + vtable_guest + """
""" + vtable_host + "\n}"

proto_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
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

proto_host = """    Scope scope(engine);
    ScopedValue protectThis(scope, this);
    if (proto)
        proto->setUsedAsProto();
    Q_ASSERT(prototype != proto);
    Q_ASSERT(!proto || proto->internalClass->isUsedAsProto());

    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::PrototypeChange };
    temp.prototype = proto;

    Transition &t = lookupOrInsertTransition(temp);
    if (t.lookup)
        return t.lookup;

    // create a new class and add it to the tree
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    QV4::WriteBarrier::markCustom(engine, [&](QV4::MarkStack *stack) {
        if (proto && QV4::WriteBarrier::isInsertionBarrier)
            proto->mark(stack);
    });
    newClass->prototype = proto;

    t.lookup = newClass;
    return prototype ? cleanInternalClass(newClass) : newClass;
#endif"""

proto_anchor = """Heap::InternalClass *InternalClass::changePrototypeImpl(Heap::Object *proto)
{
    Scope scope(engine);
    ScopedValue protectThis(scope, this);
    if (proto)
        proto->setUsedAsProto();
    Q_ASSERT(prototype != proto);
    Q_ASSERT(!proto || proto->internalClass->isUsedAsProto());

    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::PrototypeChange };
    temp.prototype = proto;

    Transition &t = lookupOrInsertTransition(temp);
    if (t.lookup)
        return t.lookup;

    // create a new class and add it to the tree
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    QV4::WriteBarrier::markCustom(engine, [&](QV4::MarkStack *stack) {
        if (proto && QV4::WriteBarrier::isInsertionBarrier)
            proto->mark(stack);
    });
    newClass->prototype = proto;

    t.lookup = newClass;
    return prototype ? cleanInternalClass(newClass) : newClass;
}"""

proto_stub = """Heap::InternalClass *InternalClass::changePrototypeImpl(Heap::Object *proto)
{
""" + proto_guest + """
""" + proto_host + "\n}"""

changed = False

# Fix lookup
if lookup_guest in text and lookup_tail in text:
    print("[patch_qv4internalclass_guest] lookup if/else already applied")
elif lookup_anchor in text:
    text = text.replace(lookup_anchor, lookup_stub, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (lookup if/else)")
else:
    # upgrade partial patches
    partial = """InternalClassTransition &InternalClass::lookupOrInsertTransition(const InternalClassTransition &t)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (transitions.capacity() == 0)
        new (&transitions) QVarLengthArray<Transition, 1>();
    for (qsizetype i = 0; i < transitions.size(); ++i) {
        if (transitions.at(i) == t)
            return transitions[i];
    }
    const qsizetype n = transitions.size();
    transitions.resize(n + 1);
    transitions[n] = t;
    return transitions[n];
#endif
    QVarLengthArray<Transition, 1>::iterator it = std::lower_bound(transitions.begin(), transitions.end(), t);"""
    if partial in text:
        text = text.replace(partial, lookup_stub.split("\n", 1)[0] + "\n" + lookup_guest + "\n" + lookup_tail.split("\n", 1)[1], 1)
        # simpler: replace whole function via regex-like manual
        start = text.find("InternalClassTransition &InternalClass::lookupOrInsertTransition")
        end = text.find("\n}\n\nstatic void addDummyEntry", start)
        if start >= 0 and end >= 0:
            text = text[:start] + lookup_stub + text[end+2:]
            changed = True
            print("[patch_qv4internalclass_guest] ok (upgrade lookup to if/else)")
    else:
        raise SystemExit("qv4internalclass.cpp: lookup anchor missing")

# Fix changeVTable
vtable_old_direct = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *newClass = engine->newClass(this);
    if (!newClass)
        return this;
    newClass->vtable = vt;
    ++newClass->numRedundantTransitions;
    return newClass;
#else"""

vtable_old_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
    Scope scope(engine);
    Scoped<QV4::InternalClass> scopedNewClass(scope, engine->newClass(this));
    auto newClass = scopedNewClass->d();
    newClass->vtable = vt;
    return vtable == QV4::InternalClass::staticVTable()
            ? newClass
            : cleanInternalClass(newClass);
#else"""

if vtable_guest in text:
    print("[patch_qv4internalclass_guest] changeVTable eng-fallback already applied")
elif vtable_old_direct in text:
    text = text.replace(vtable_old_direct, vtable_guest, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (changeVTable eng-fallback)")
elif vtable_old_guest in text:
    text = text.replace(vtable_old_guest, vtable_guest, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (changeVTable direct heap)")
elif vtable_broken in text:
    text = text.replace(vtable_broken, vtable_guest + "\n" + vtable_host, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (fix changeVTable if/else)")
elif vtable_anchor in text:
    text = text.replace(vtable_anchor, vtable_stub, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (changeVTable if/else)")
else:
    start = text.find("Heap::InternalClass *InternalClass::changeVTableImpl")
    end = text.find("\n}\n\nHeap::InternalClass *InternalClass::nonExtensible", start)
    if start >= 0 and end >= 0:
        text = text[:start] + vtable_stub + text[end+2:]
        changed = True
        print("[patch_qv4internalclass_guest] ok (rewrite changeVTable)")
    else:
        print("[patch_qv4internalclass_guest] warn: changeVTable anchor missing")

# changePrototype guest bypass
if proto_guest in text:
    print("[patch_qv4internalclass_guest] changePrototype bypass already applied")
elif proto_anchor in text:
    text = text.replace(proto_anchor, proto_stub, 1)
    changed = True
    print("[patch_qv4internalclass_guest] ok (changePrototype bypass)")
else:
    print("[patch_qv4internalclass_guest] warn: changePrototype anchor missing")

if changed:
    path.write_text(text)

# Guest init: assign engine before subobject constructors (IC heap may not persist engine field otherwise).
init_path = path
init_text = init_path.read_text()
init_anchor = """void InternalClass::init(ExecutionEngine *engine)
{
//    InternalClass is automatically zeroed during allocation:
//    prototype = nullptr;
//    parent = nullptr;
//    size = 0;
//    numRedundantTransitions = 0;
//    flags = 0;

    Base::init();"""
init_guest = """void InternalClass::init(ExecutionEngine *engine)
{
//    InternalClass is automatically zeroed during allocation:
//    prototype = nullptr;
//    parent = nullptr;
//    size = 0;
//    numRedundantTransitions = 0;
//    flags = 0;

#if defined(BFREE_GUEST_FIXED_STACK)
    this->engine = engine;
#endif
    Base::init();"""
if "#if defined(BFREE_GUEST_FIXED_STACK)\n    this->engine = engine;\n#endif\n    Base::init();" in init_text:
    print("[patch_qv4internalclass_guest] init engine-first already applied")
elif init_anchor in init_text:
    init_text = init_text.replace(init_anchor, init_guest, 1)
    init_path.write_text(init_text)
    changed = True
    print("[patch_qv4internalclass_guest] ok (init engine-first)")
else:
    print("[patch_qv4internalclass_guest] warn: init anchor missing")

init_other_anchor = """void InternalClass::init(Heap::InternalClass *other)
{
    Base::init();
    new (&propertyTable) PropertyHash(other->propertyTable);
    new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    new (&transitions) QVarLengthArray<Transition, 1>();

    engine = other->engine;"""

init_other_guest = """void InternalClass::init(Heap::InternalClass *other)
{
#if defined(BFREE_GUEST_FIXED_STACK)
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
#endif
    Base::init();
    new (&propertyTable) PropertyHash(other->propertyTable);
    new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    new (&transitions) QVarLengthArray<Transition, 1>();

    engine = other->engine;"""

init_text = init_path.read_text()
if "#if defined(BFREE_GUEST_FIXED_STACK)\n    Base::init();\n    ExecutionEngine *eng = other->engine;" in init_text:
    print("[patch_qv4internalclass_guest] init(other) guest already applied")
elif init_other_anchor in init_text:
    init_text = init_text.replace(init_other_anchor, init_other_guest, 1)
    init_path.write_text(init_text)
    print("[patch_qv4internalclass_guest] ok (init other guest)")
else:
    print("[patch_qv4internalclass_guest] warn: init(other) anchor missing")

clean_anchor = """static Heap::InternalClass *cleanInternalClass(Heap::InternalClass *orig)
{
    if (++orig->numRedundantTransitions < Heap::InternalClass::MaxRedundantTransitions)
        return orig;"""

clean_guest = """static Heap::InternalClass *cleanInternalClass(Heap::InternalClass *orig)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    ++orig->numRedundantTransitions;
    return orig;
#endif
    if (++orig->numRedundantTransitions < Heap::InternalClass::MaxRedundantTransitions)
        return orig;"""

asproto_anchor = """Heap::InternalClass *InternalClass::asProtoClass()
{
    if (isUsedAsProto())
        return this;

    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::ProtoClass };"""

asproto_stub = """Heap::InternalClass *InternalClass::asProtoClass()
{
    if (isUsedAsProto())
        return this;

#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    newClass->flags |= UsedAsProto;
    return newClass;
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
}"""

asproto_broken = """Heap::InternalClass *InternalClass::asProtoClass()
{
    if (isUsedAsProto())
        return this;

#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    newClass->flags |= UsedAsProto;
    return newClass;
#endif

    Transition temp = { { PropertyKey::invalid() }, nullptr, Transition::ProtoClass };"""

init_text = init_path.read_text()
if clean_guest.split("#endif")[0] in init_text:
    print("[patch_qv4internalclass_guest] cleanInternalClass guest already applied")
elif clean_anchor in init_text:
    init_text = init_text.replace(clean_anchor, clean_guest, 1)
    init_path.write_text(init_text)
    print("[patch_qv4internalclass_guest] ok (cleanInternalClass guest)")
else:
    print("[patch_qv4internalclass_guest] warn: cleanInternalClass anchor missing")

init_text = init_path.read_text()
if asproto_stub.split("#else")[0] + "#endif" in init_text or "newClass->flags |= UsedAsProto;\n    return newClass;\n#else" in init_text:
    print("[patch_qv4internalclass_guest] asProtoClass guest already applied")
elif asproto_broken in init_text:
    start = init_text.find("Heap::InternalClass *InternalClass::asProtoClass()")
    end = init_text.find("\n}\n\nstatic void updateProtoUsage", start)
    if start >= 0 and end >= 0:
        init_text = init_text[:start] + asproto_stub + init_text[end+2:]
        init_path.write_text(init_text)
        print("[patch_qv4internalclass_guest] ok (fix asProtoClass if/else)")
    else:
        print("[patch_qv4internalclass_guest] warn: asProtoClass broken rewrite failed")
elif asproto_anchor in init_text:
    init_text = init_text.replace(asproto_anchor, asproto_stub, 1)
    init_path.write_text(init_text)
    print("[patch_qv4internalclass_guest] ok (asProtoClass guest)")
else:
    print("[patch_qv4internalclass_guest] warn: asProtoClass anchor missing")

print("[patch_qv4internalclass_guest] done")
PY
