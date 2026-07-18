from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

gen_old = """    bfree_guest_qv4_heartbeat("generator_ic_enter");
    if (jsObjects[GeneratorProto].isManaged())
        classes[Class_GeneratorObject] = newInternalClass(QV4::GeneratorObject::staticVTable(), generatorPrototype());
    bfree_guest_qv4_heartbeat(classes[Class_GeneratorObject] ? "generator_ic_ok" : "generator_ic_null");"""

gen_new = """    bfree_guest_qv4_heartbeat("generator_ic_enter");
    {
        Heap::InternalClass *genIc = classes[Class_Empty]
                ? classes[Class_Empty]->changeVTable(QV4::GeneratorObject::staticVTable()) : nullptr;
        Heap::Object *genProtoHeap = jsObjects[GeneratorProto].isManaged()
                ? static_cast<Heap::Object *>(jsObjects[GeneratorProto].m()) : nullptr;
        if (genIc && genProtoHeap)
            genIc = genIc->changePrototype(genProtoHeap);
        classes[Class_GeneratorObject] = genIc;
    }
    bfree_guest_qv4_heartbeat(classes[Class_GeneratorObject] ? "generator_ic_ok" : "generator_ic_null");"""

re_old = """    ScopedString str(scope);
    classes[Class_RegExp] = classes[Class_Empty]->changeVTable(QV4::RegExp::staticVTable());
    ic = newInternalClass(QV4::RegExpObject::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("lastIndex")), Attr_NotEnumerable|Attr_NotConfigurable, index);
    Q_ASSERT(index->index == RegExpObject::Index_LastIndex);
    jsObjects[RegExpProto] = memoryManager->allocObject<RegExpPrototype>(classes[Class_Object]);
    classes[Class_RegExpObject] = ic->changePrototype(regExpPrototype()->d());

    ic = classes[Class_ArrayObject]->addMember(guestBuiltinStringKey(this, QStringLiteral("index")), Attr_Data, index);
    Q_ASSERT(index->index == RegExpObject::Index_ArrayIndex);
    classes[Class_RegExpExecArray] = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("input")), Attr_Data, index);
    Q_ASSERT(index->index == RegExpObject::Index_ArrayInput);

    ic = newInternalClass(ErrorObject::staticVTable(), nullptr);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("stack")))->propertyKey(), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable, index);"""

re_new = """    ScopedString str(scope);
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("regexp_enter");
    if (classes[Class_Empty])
        classes[Class_RegExp] = classes[Class_Empty]->changeVTable(QV4::RegExp::staticVTable());
    ic = newInternalClass(QV4::RegExpObject::staticVTable(), objectPrototype());
    bfree_guest_qv4_heartbeat(ic ? "regexp_ic_ok" : "regexp_ic_null");
    if (ic)
        ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("lastIndex")), Attr_NotEnumerable|Attr_NotConfigurable, index);
    if (classes[Class_Object]) {
        if (auto *rp = memoryManager->allocateObject<RegExpPrototype>(classes[Class_Object]))
            jsObjects[RegExpProto] = rp;
    }
    bfree_guest_qv4_heartbeat(jsObjects[RegExpProto].isManaged() ? "regexp_proto_ok" : "regexp_proto_null");
    if (ic && jsObjects[RegExpProto].isManaged()) {
        Heap::Object *rpHeap = static_cast<Heap::Object *>(jsObjects[RegExpProto].m());
        if (rpHeap)
            classes[Class_RegExpObject] = ic->changePrototype(rpHeap);
    }
    bfree_guest_qv4_heartbeat(classes[Class_RegExpObject] ? "regexp_obj_ok" : "regexp_obj_null");
    if (classes[Class_ArrayObject]) {
        ic = classes[Class_ArrayObject]->addMember(guestBuiltinStringKey(this, QStringLiteral("index")), Attr_Data, index);
        if (ic)
            classes[Class_RegExpExecArray] = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("input")), Attr_Data, index);
    }
    bfree_guest_qv4_heartbeat(classes[Class_RegExpExecArray] ? "regexp_exec_ok" : "regexp_exec_null");
    ic = newInternalClass(ErrorObject::staticVTable(), nullptr);
    bfree_guest_qv4_heartbeat(ic ? "error_ic_ok" : "error_ic_null");
    if (ic)
        ic = ic->addMember((str = newIdentifier(QStringLiteral("stack")))->propertyKey(), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable, index);
#else
    classes[Class_RegExp] = classes[Class_Empty]->changeVTable(QV4::RegExp::staticVTable());
    ic = newInternalClass(QV4::RegExpObject::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("lastIndex")), Attr_NotEnumerable|Attr_NotConfigurable, index);
    Q_ASSERT(index->index == RegExpObject::Index_LastIndex);
    jsObjects[RegExpProto] = memoryManager->allocObject<RegExpPrototype>(classes[Class_Object]);
    classes[Class_RegExpObject] = ic->changePrototype(regExpPrototype()->d());

    ic = classes[Class_ArrayObject]->addMember(guestBuiltinStringKey(this, QStringLiteral("index")), Attr_Data, index);
    Q_ASSERT(index->index == RegExpObject::Index_ArrayIndex);
    classes[Class_RegExpExecArray] = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("input")), Attr_Data, index);
    Q_ASSERT(index->index == RegExpObject::Index_ArrayInput);

    ic = newInternalClass(ErrorObject::staticVTable(), nullptr);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("stack")))->propertyKey(), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable, index);
#endif"""

changed = False
if gen_new.split("genProtoHeap")[0] in te:
    print("[v216] generator_ic heap proto already patched")
elif gen_old in te:
    te = te.replace(gen_old, gen_new, 1)
    changed = True
    print("[v216] generator_ic via heap proto + changePrototype")
else:
    raise SystemExit("[v216] generator_ic anchor missing")

if "regexp_enter" in te:
    print("[v216] regexp guest block already patched")
elif re_old not in te:
    raise SystemExit("[v216] regexp anchor missing")
else:
    te = te.replace(re_old, re_new, 1)
    changed = True
    print("[v216] regexp/error guest guards + heartbeats")

if changed:
    eng.write_text(te)
print("[v216] done")
