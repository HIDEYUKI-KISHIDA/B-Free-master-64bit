from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

re_proto_old = """    if (ic && jsObjects[RegExpProto].isManaged()) {
        Heap::Object *rpHeap = static_cast<Heap::Object *>(jsObjects[RegExpProto].m());
        if (rpHeap)
            classes[Class_RegExpObject] = ic->changePrototype(rpHeap);
    }"""

re_proto_new = """    if (ic && jsObjects[RegExpProto].isManaged()) {
        Heap::Object *rpHeap = static_cast<Heap::Object *>(jsObjects[RegExpProto].m());
        if (rpHeap) {
            rpHeap->setUsedAsProto();
            classes[Class_RegExpObject] = ic->changePrototype(rpHeap);
        }
    }"""

err_stack_old = """    ic = newInternalClass(ErrorObject::staticVTable(), nullptr);
    bfree_guest_qv4_heartbeat(ic ? "error_ic_ok" : "error_ic_null");
    if (ic)
        ic = ic->addMember((str = newIdentifier(QStringLiteral("stack")))->propertyKey(), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable, index);
#else"""

err_stack_new = """    ic = newInternalClass(ErrorObject::staticVTable(), nullptr);
    bfree_guest_qv4_heartbeat(ic ? "error_ic_ok" : "error_ic_null");
#else"""

post_old = """#endif
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

    classes[Class_ProxyObject] = classes[Class_Empty]->changeVTable(ProxyObject::staticVTable());"""

post_new = """#endif
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("error_props_skip");
    if (ic)
        classes[Class_ErrorObject] = ic->d();
    else if (classes[Class_Object])
        classes[Class_ErrorObject] = classes[Class_Object];
    classes[Class_ErrorObjectWithMessage] = classes[Class_ErrorObject];
    classes[Class_ErrorProto] = classes[Class_Object];
    bfree_guest_qv4_heartbeat("error_classes_ok");
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

    classes[Class_ProxyObject] = classes[Class_Empty]->changeVTable(ProxyObject::staticVTable());"""

for label, old, new in [
    ("regexp setUsedAsProto", re_proto_old, re_proto_new),
    ("error stack skip", err_stack_old, err_stack_new),
    ("error props guest skip", post_old, post_new),
]:
    if new.split("error_props_skip")[0] in te and label == "error props guest skip":
        print(f"[v217] {label} already patched")
    elif old in te:
        te = te.replace(old, new, 1)
        print(f"[v217] {label}")
    elif label == "error props guest skip" and "error_props_skip" in te:
        print(f"[v217] {label} already patched")
    elif label == "error stack skip" and "error_ic_ok" in te and "error_props_skip" in te:
        print(f"[v217] {label} already patched")
    elif label == "regexp setUsedAsProto" and re_proto_new.split("setUsedAsProto")[0] in te:
        print(f"[v217] {label} already patched")
    else:
        raise SystemExit(f"[v217] anchor missing: {label}")
print("[v217] done")
