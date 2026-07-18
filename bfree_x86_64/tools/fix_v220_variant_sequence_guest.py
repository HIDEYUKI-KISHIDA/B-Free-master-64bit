from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

class_old = """    Heap::InternalClass *classObject = objectClass && jsObjects[ObjectProto].isManaged()
            ? objectClass->changePrototype(objectPrototype()->d()) : nullptr;
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");"""

class_new = """    Heap::InternalClass *classObject = nullptr;
    if (objectClass && jsObjects[ObjectProto].isManaged()) {
        Heap::Object *opHeap = static_cast<Heap::Object *>(jsObjects[ObjectProto].m());
        if (opHeap) {
            opHeap->setUsedAsProto();
            classObject = objectClass->changePrototype(opHeap);
        }
    }
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");"""

post_old = """    bfree_guest_qv4_heartbeat("error_subproto_skip");
#else
    classes[Class_ProxyObject] = classes[Class_Empty]->changeVTable(ProxyObject::staticVTable());
    classes[Class_ProxyFunctionObject] = classes[Class_Empty]->changeVTable(ProxyFunctionObject::staticVTable());

    jsObjects[GetStack_Function] = FunctionObject::createBuiltinFunction(this, str = newIdentifier(QStringLiteral("stack")), ErrorObject::method_get_stack, 0);

    jsObjects[ErrorProto] = memoryManager->allocObject<ErrorPrototype>(classes[Class_ErrorProto]);
    ic = classes[Class_ErrorProto]->changePrototype(errorPrototype()->d());
    jsObjects[EvalErrorProto] = memoryManager->allocObject<EvalErrorPrototype>(ic->d());
    jsObjects[RangeErrorProto] = memoryManager->allocObject<RangeErrorPrototype>(ic->d());
    jsObjects[ReferenceErrorProto] = memoryManager->allocObject<ReferenceErrorPrototype>(ic->d());
    jsObjects[SyntaxErrorProto] = memoryManager->allocObject<SyntaxErrorPrototype>(ic->d());
    jsObjects[TypeErrorProto] = memoryManager->allocObject<TypeErrorPrototype>(ic->d());
    jsObjects[URIErrorProto] = memoryManager->allocObject<URIErrorPrototype>(ic->d());
#endif

    jsObjects[VariantProto] = memoryManager->allocate<VariantPrototype>();
    Q_ASSERT(variantPrototype()->getPrototypeOf() == objectPrototype()->d());

    ic = newInternalClass(SequencePrototype::staticVTable(), SequencePrototype::defaultPrototype(this));
    jsObjects[SequenceProto] = ScopedValue(scope, memoryManager->allocObject<SequencePrototype>(ic->d()));

    jsObjects[Object_Ctor] = memoryManager->allocate<ObjectCtor>(this);"""

post_new = """    bfree_guest_qv4_heartbeat("error_subproto_skip");
    bfree_guest_qv4_heartbeat("variant_enter");
    if (auto *vp = memoryManager->allocate<VariantPrototype>())
        jsObjects[VariantProto] = vp;
    bfree_guest_qv4_heartbeat(jsObjects[VariantProto].isManaged() ? "variant_ok" : "variant_null");
    bfree_guest_qv4_heartbeat("sequence_enter");
    {
        Heap::InternalClass *seqIc = classes[Class_Empty]
                ? classes[Class_Empty]->changeVTable(SequencePrototype::staticVTable()) : nullptr;
        Heap::Object *opHeap = jsObjects[ObjectProto].isManaged()
                ? static_cast<Heap::Object *>(jsObjects[ObjectProto].m()) : nullptr;
        if (seqIc && opHeap)
            seqIc = seqIc->changePrototype(opHeap);
        if (seqIc)
            jsObjects[SequenceProto] = ScopedValue(scope, memoryManager->allocObject<SequencePrototype>(seqIc));
    }
    bfree_guest_qv4_heartbeat(jsObjects[SequenceProto].isManaged() ? "sequence_ok" : "sequence_null");
    bfree_guest_qv4_heartbeat("ctors_enter");
#else
    classes[Class_ProxyObject] = classes[Class_Empty]->changeVTable(ProxyObject::staticVTable());
    classes[Class_ProxyFunctionObject] = classes[Class_Empty]->changeVTable(ProxyFunctionObject::staticVTable());

    jsObjects[GetStack_Function] = FunctionObject::createBuiltinFunction(this, str = newIdentifier(QStringLiteral("stack")), ErrorObject::method_get_stack, 0);

    jsObjects[ErrorProto] = memoryManager->allocObject<ErrorPrototype>(classes[Class_ErrorProto]);
    ic = classes[Class_ErrorProto]->changePrototype(errorPrototype()->d());
    jsObjects[EvalErrorProto] = memoryManager->allocObject<EvalErrorPrototype>(ic->d());
    jsObjects[RangeErrorProto] = memoryManager->allocObject<RangeErrorPrototype>(ic->d());
    jsObjects[ReferenceErrorProto] = memoryManager->allocObject<ReferenceErrorPrototype>(ic->d());
    jsObjects[SyntaxErrorProto] = memoryManager->allocObject<SyntaxErrorPrototype>(ic->d());
    jsObjects[TypeErrorProto] = memoryManager->allocObject<TypeErrorPrototype>(ic->d());
    jsObjects[URIErrorProto] = memoryManager->allocObject<URIErrorPrototype>(ic->d());

    jsObjects[VariantProto] = memoryManager->allocate<VariantPrototype>();
    Q_ASSERT(variantPrototype()->getPrototypeOf() == objectPrototype()->d());

    ic = newInternalClass(SequencePrototype::staticVTable(), SequencePrototype::defaultPrototype(this));
    jsObjects[SequenceProto] = ScopedValue(scope, memoryManager->allocObject<SequencePrototype>(ic->d()));
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("object_ctor_enter");
#endif
    jsObjects[Object_Ctor] = memoryManager->allocate<ObjectCtor>(this);"""

changed = False
if class_new.split("opHeap->setUsedAsProto")[0] in te or "opHeap = memoryManager->allocObject<ObjectPrototype>" in te:
    print("[v220] class_object heap proto path already patched")
elif class_old not in te:
    raise SystemExit("[v220] class_object anchor missing")
else:
    te = te.replace(class_old, class_new, 1)
    changed = True
    print("[v220] class_object heap proto path")

if "variant_enter" in te:
    print("[v220] variant/sequence guest already patched")
elif post_old not in te:
    raise SystemExit("[v220] post-error anchor missing")
else:
    te = te.replace(post_old, post_new, 1)
    changed = True
    print("[v220] variant/sequence guest skip + heartbeats")

if changed:
    eng.write_text(te)
print("[v220] done")
