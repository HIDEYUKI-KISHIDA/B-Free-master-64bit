from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = eng.read_text()

shared_start = """    ic = newInternalClass(QV4::FunctionPrototype::staticVTable(), objectPrototype());
    auto addProtoHasInstance = [&] {
        // Add an invalid prototype slot, so that all function objects have the same layout
        // This helps speed up instanceof operations and other things where we need to query
        // prototype property (as we always know it's location)
        ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        Q_ASSERT(index->index == Heap::FunctionObject::Index_Prototype);
        // add an invalid @hasInstance slot, so that we can quickly track whether the
        // hasInstance method has been reimplemented. This is required for a fast
        // instanceof implementation
        ic = ic->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
        Q_ASSERT(index->index == Heap::FunctionObject::Index_HasInstance);
    };
    addProtoHasInstance();
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat(ic ? "func_proto_ic_ok" : "func_proto_ic_null");
    if (ic) {
        if (auto *fp = memoryManager->allocateObject<FunctionPrototype>(ic->d())) {
            jsObjects[FunctionProto] = fp;
            bfree_guest_qv4_heartbeat("func_proto_alloc");
        } else {
            bfree_guest_qv4_heartbeat("func_proto_alloc_null");
        }
    }
#else
    jsObjects[FunctionProto] = memoryManager->allocObject<FunctionPrototype>(ic->d());
#endif
    ic = newInternalClass(FunctionObject::staticVTable(), functionPrototype());
    addProtoHasInstance();
    classes[Class_FunctionObject] = ic->d();
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
    Q_ASSERT(index->index == Heap::ArrowFunction::Index_Name);
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    Q_ASSERT(index->index == Heap::ArrowFunction::Index_Length);
    classes[Class_ArrowFunction] = ic->changeVTable(ArrowFunction::staticVTable());
    ic = ic->changeVTable(MemberFunction::staticVTable());
    classes[Class_MemberFunction] = ic->d();
    ic = ic->changeVTable(GeneratorFunction::staticVTable());
    classes[Class_GeneratorFunction] = ic->d();
    ic = ic->changeVTable(MemberGeneratorFunction::staticVTable());
    classes[Class_MemberGeneratorFunction] = ic->d();

    ic = ic->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    ic = ic->changeVTable(ScriptFunction::staticVTable());
    classes[Class_ScriptFunction] = ic->d();
    ic = ic->changeVTable(ConstructorFunction::staticVTable());
    classes[Class_ConstructorFunction] = ic->d();"""

guest_block = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *funcProtoIc = newInternalClass(QV4::FunctionPrototype::staticVTable(), objectPrototype());
    if (funcProtoIc) {
        funcProtoIc = funcProtoIc->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        Q_ASSERT(!index || index->index == Heap::FunctionObject::Index_Prototype);
        if (funcProtoIc) {
            funcProtoIc = funcProtoIc->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
            Q_ASSERT(!index || index->index == Heap::FunctionObject::Index_HasInstance);
        }
    }
    bfree_guest_qv4_heartbeat(funcProtoIc ? "func_proto_ic_ok" : "func_proto_ic_null");
    Heap::FunctionPrototype *fp = funcProtoIc
            ? memoryManager->allocateObject<FunctionPrototype>(funcProtoIc) : nullptr;
    if (fp)
        jsObjects[FunctionProto] = fp;
    bfree_guest_qv4_heartbeat(fp ? "func_proto_alloc" : "func_proto_alloc_null");

    bfree_guest_qv4_heartbeat("func_classes_enter");
    Heap::InternalClass *funcObjIc = newInternalClass(FunctionObject::staticVTable(), functionPrototype());
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
    }
    classes[Class_FunctionObject] = funcObjIc;
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    }
    if (funcObjIc)
        classes[Class_ArrowFunction] = funcObjIc->changeVTable(ArrowFunction::staticVTable());
    if (classes[Class_ArrowFunction])
        classes[Class_MemberFunction] = classes[Class_ArrowFunction]->changeVTable(MemberFunction::staticVTable());
    if (classes[Class_MemberFunction])
        classes[Class_GeneratorFunction] = classes[Class_MemberFunction]->changeVTable(GeneratorFunction::staticVTable());
    if (classes[Class_GeneratorFunction])
        classes[Class_MemberGeneratorFunction] = classes[Class_GeneratorFunction]->changeVTable(MemberGeneratorFunction::staticVTable());

    Heap::InternalClass *funcClassIc = classes[Class_MemberGeneratorFunction];
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ScriptFunction::staticVTable());
    classes[Class_ScriptFunction] = funcClassIc;
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ConstructorFunction::staticVTable());
    classes[Class_ConstructorFunction] = funcClassIc;
    if (funcClassIc)
        ic = funcClassIc;
    bfree_guest_qv4_heartbeat("func_classes_done");
#else
    ic = newInternalClass(QV4::FunctionPrototype::staticVTable(), objectPrototype());
    auto addProtoHasInstance = [&] {
        // Add an invalid prototype slot, so that all function objects have the same layout
        // This helps speed up instanceof operations and other things where we need to query
        // prototype property (as we always know it's location)
        ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        Q_ASSERT(index->index == Heap::FunctionObject::Index_Prototype);
        // add an invalid @hasInstance slot, so that we can quickly track whether the
        // hasInstance method has been reimplemented. This is required for a fast
        // instanceof implementation
        ic = ic->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
        Q_ASSERT(index->index == Heap::FunctionObject::Index_HasInstance);
    };
    addProtoHasInstance();
    jsObjects[FunctionProto] = memoryManager->allocObject<FunctionPrototype>(ic->d());
    ic = newInternalClass(FunctionObject::staticVTable(), functionPrototype());
    addProtoHasInstance();
    classes[Class_FunctionObject] = ic->d();
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
    Q_ASSERT(index->index == Heap::ArrowFunction::Index_Name);
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    Q_ASSERT(index->index == Heap::ArrowFunction::Index_Length);
    classes[Class_ArrowFunction] = ic->changeVTable(ArrowFunction::staticVTable());
    ic = ic->changeVTable(MemberFunction::staticVTable());
    classes[Class_MemberFunction] = ic->d();
    ic = ic->changeVTable(GeneratorFunction::staticVTable());
    classes[Class_GeneratorFunction] = ic->d();
    ic = ic->changeVTable(MemberGeneratorFunction::staticVTable());
    classes[Class_MemberGeneratorFunction] = ic->d();

    ic = ic->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    ic = ic->changeVTable(ScriptFunction::staticVTable());
    classes[Class_ScriptFunction] = ic->d();
    ic = ic->changeVTable(ConstructorFunction::staticVTable());
    classes[Class_ConstructorFunction] = ic->d();
#endif"""

if "func_classes_done" in t:
    print("[v207] func proto guest block already patched")
elif shared_start not in t:
    raise SystemExit("[v207] FunctionPrototype anchor missing")
else:
    t = t.replace(shared_start, guest_block, 1)
    print("[v207] FunctionPrototype raw ic + func_classes guest block")

eng.write_text(t)
print("[v207] done")
