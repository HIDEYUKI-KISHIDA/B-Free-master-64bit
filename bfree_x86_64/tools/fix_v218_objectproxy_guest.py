from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

obj_old = """    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    bfree_guest_qv4_heartbeat("class_object_vt");
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(objectClass);
    Heap::InternalClass *classObject = objectClass ? objectClass->changePrototype(objectPrototype()->d()) : nullptr;
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");"""

obj_new = """    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    bfree_guest_qv4_heartbeat("class_object_vt");
    if (objectClass) {
        if (auto *op = memoryManager->allocateObject<ObjectPrototype>(objectClass))
            jsObjects[ObjectProto] = op;
    }
    bfree_guest_qv4_heartbeat(jsObjects[ObjectProto].isManaged() ? "object_proto_alloc_ok" : "object_proto_alloc_null");
    Heap::InternalClass *classObject = nullptr;
    if (objectClass && jsObjects[ObjectProto].isManaged()) {
        Heap::Object *opHeap = static_cast<Heap::Object *>(jsObjects[ObjectProto].m());
        if (opHeap) {
            opHeap->setUsedAsProto();
            classObject = objectClass->changePrototype(opHeap);
        }
    }
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");"""

post_err_old = """    bfree_guest_qv4_heartbeat("error_classes_ok");
#else
    Q_ASSERT(index->index == ErrorObject::Index_Stack);
    Q_ASSERT(index->setterIndex == ErrorObject::Index_StackSetter);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("fileName")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorObject::Index_FileName);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("lineNumber")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    classes[Class_ErrorObject] = ic->d();
    Q_ASSERT(index->index == ErrorObject::Index_LineNumber);
    classes[Class_ErrorObjectWithMessage] = ic->addMember((str = newIdentifier(QStringLiteral("message")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorObject::Index_Message);
    ic = newInternalClass(Object::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("constructor")), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Constructor);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("message")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Message);
    classes[Class_ErrorProto] = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Name);
#endif

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
    jsObjects[URIErrorProto] = memoryManager->allocObject<URIErrorPrototype>(ic->d());"""

post_err_new = """    bfree_guest_qv4_heartbeat("error_classes_ok");
#else
    Q_ASSERT(index->index == ErrorObject::Index_Stack);
    Q_ASSERT(index->setterIndex == ErrorObject::Index_StackSetter);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("fileName")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorObject::Index_FileName);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("lineNumber")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    classes[Class_ErrorObject] = ic->d();
    Q_ASSERT(index->index == ErrorObject::Index_LineNumber);
    classes[Class_ErrorObjectWithMessage] = ic->addMember((str = newIdentifier(QStringLiteral("message")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorObject::Index_Message);
    ic = newInternalClass(Object::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("constructor")), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Constructor);
    ic = ic->addMember((str = newIdentifier(QStringLiteral("message")))->propertyKey(), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Message);
    classes[Class_ErrorProto] = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_Data|Attr_NotEnumerable, index);
    Q_ASSERT(index->index == ErrorPrototype::Index_Name);
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("proxy_enter");
    if (classes[Class_Empty]) {
        classes[Class_ProxyObject] = classes[Class_Empty]->changeVTable(ProxyObject::staticVTable());
        classes[Class_ProxyFunctionObject] = classes[Class_Empty]->changeVTable(ProxyFunctionObject::staticVTable());
    }
    bfree_guest_qv4_heartbeat("proxy_ok");
    bfree_guest_qv4_heartbeat("getstack_skip");
    if (classes[Class_ErrorProto] && jsObjects[ObjectProto].isManaged()) {
        if (auto *ep = memoryManager->allocateObject<ErrorPrototype>(classes[Class_ErrorProto]))
            jsObjects[ErrorProto] = ep;
    }
    bfree_guest_qv4_heartbeat(jsObjects[ErrorProto].isManaged() ? "error_proto_ok" : "error_proto_null");
    bfree_guest_qv4_heartbeat("error_subproto_skip");
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
#endif"""

changed = False
if obj_new.split("object_proto_alloc_ok")[0] in te or "object_proto_pre" in te:
    print("[v218] class_object heap proto already patched")
elif obj_old not in te:
    raise SystemExit("[v218] class_object anchor missing")
else:
    te = te.replace(obj_old, obj_new, 1)
    changed = True
    print("[v218] class_object heap ObjectProto path")

if "proxy_enter" in te:
    print("[v218] proxy/error proto guest already patched")
elif post_err_old not in te:
    raise SystemExit("[v218] post-error anchor missing")
else:
    te = te.replace(post_err_old, post_err_new, 1)
    changed = True
    print("[v218] proxy + error proto guest skip")

if changed:
    eng.write_text(te)
print("[v218] done")
