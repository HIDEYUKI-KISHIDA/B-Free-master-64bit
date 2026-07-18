from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

old = """    classes[Class_ObjectProto] = classes[Class_Object]->addMember(guestBuiltinStringKey(this, QStringLiteral("constructor")), Attr_NotEnumerable, index);
    Q_ASSERT(index->index == Heap::FunctionObject::Index_ProtoConstructor);

    jsObjects[GeneratorProto] = memoryManager->allocObject<GeneratorPrototype>(classes[Class_Object]);
    classes[Class_GeneratorObject] = newInternalClass(QV4::GeneratorObject::staticVTable(), generatorPrototype());"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("object_proto_enter");
#endif
    classes[Class_ObjectProto] = classes[Class_Object]->addMember(guestBuiltinStringKey(this, QStringLiteral("constructor")), Attr_NotEnumerable, index);
    Q_ASSERT(index->index == Heap::FunctionObject::Index_ProtoConstructor);
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat(classes[Class_ObjectProto] ? "object_proto_ok" : "object_proto_null");
    bfree_guest_qv4_heartbeat("generator_proto_enter");
    if (classes[Class_Object]) {
        if (auto *gp = memoryManager->allocateObject<GeneratorPrototype>(classes[Class_Object]))
            jsObjects[GeneratorProto] = gp;
        bfree_guest_qv4_heartbeat(jsObjects[GeneratorProto].isManaged() ? "generator_proto_ok" : "generator_proto_null");
    } else {
        bfree_guest_qv4_heartbeat("generator_proto_no_obj_ic");
    }
    bfree_guest_qv4_heartbeat("generator_ic_enter");
    if (jsObjects[GeneratorProto].isManaged())
        classes[Class_GeneratorObject] = newInternalClass(QV4::GeneratorObject::staticVTable(), generatorPrototype());
    bfree_guest_qv4_heartbeat(classes[Class_GeneratorObject] ? "generator_ic_ok" : "generator_ic_null");
#else
    jsObjects[GeneratorProto] = memoryManager->allocObject<GeneratorPrototype>(classes[Class_Object]);
    classes[Class_GeneratorObject] = newInternalClass(QV4::GeneratorObject::staticVTable(), generatorPrototype());
#endif"""

if "generator_proto_enter" in te:
    bad = """        Heap::GeneratorPrototype *gp = memoryManager->allocateObject<GeneratorPrototype>(classes[Class_Object]);
        if (gp)
            jsObjects[GeneratorProto] = gp;
        bfree_guest_qv4_heartbeat(gp ? "generator_proto_ok" : "generator_proto_null");"""
    good = """        if (auto *gp = memoryManager->allocateObject<GeneratorPrototype>(classes[Class_Object]))
            jsObjects[GeneratorProto] = gp;
        bfree_guest_qv4_heartbeat(jsObjects[GeneratorProto].isManaged() ? "generator_proto_ok" : "generator_proto_null");"""
    if bad in te:
        te = te.replace(bad, good, 1)
        eng.write_text(te)
        print("[v215] fixed GeneratorPrototype alloc type")
    else:
        print("[v215] post-func_classes heartbeats already patched")
elif old not in te:
    raise SystemExit("[v215] post-func_classes anchor missing")
else:
    te = te.replace(old, new, 1)
    print("[v215] post-func_classes heartbeats + guest GeneratorProto path")

eng.write_text(te)
print("[v215] done")
