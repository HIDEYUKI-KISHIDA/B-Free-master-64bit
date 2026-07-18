#!/usr/bin/env bash
# Guest Array/Arguments proto setup: Heap::InternalClass* (Scoped ic d() was null).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

old = """    ic = newInternalClass(ArrayPrototype::staticVTable(), objectPrototype());
    Q_ASSERT(ic->d()->prototype);
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_NotConfigurable|Attr_NotEnumerable);
    Q_ASSERT(ic->d()->prototype);
    jsObjects[ArrayProto] = memoryManager->allocObject<ArrayPrototype>(ic->d());
    classes[Class_ArrayObject] = ic->changePrototype(arrayPrototype()->d());
    jsObjects[PropertyListProto] = memoryManager->allocate<PropertyListPrototype>();

    Scoped<InternalClass> argsClass(scope);
    argsClass = newInternalClass(ArgumentsObject::staticVTable(), objectPrototype());
    argsClass = argsClass->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_NotEnumerable);
    argsClass = argsClass->addMember(guestBuiltinSymbolKey(this, Symbol_iterator), Attr_Data|Attr_NotEnumerable);
    classes[Class_ArgumentsObject] = argsClass->addMember(guestBuiltinStringKey(this, QStringLiteral("callee")), Attr_Data|Attr_NotEnumerable);
    argsClass = newInternalClass(StrictArgumentsObject::staticVTable(), objectPrototype());
    argsClass = argsClass->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_NotEnumerable);
    argsClass = argsClass->addMember(guestBuiltinSymbolKey(this, Symbol_iterator), Attr_Data|Attr_NotEnumerable);
    classes[Class_StrictArgumentsObject] = argsClass->addMember(guestBuiltinStringKey(this, QStringLiteral("callee")), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable);"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    {
        Heap::InternalClass *arrayIc = newInternalClass(ArrayPrototype::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(arrayIc ? "array_ic_ok" : "array_ic_null");
        if (arrayIc) {
            bfree_guest_qv4_heartbeat("pre_array_add");
            arrayIc = arrayIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")),
                                         Attr_NotConfigurable | Attr_NotEnumerable);
            bfree_guest_qv4_heartbeat("post_array_add");
            if (Heap::ArrayObject *arrayProto = memoryManager->allocateObject<ArrayPrototype>(arrayIc)) {
                jsObjects[ArrayProto] = Value::fromHeapObject(arrayProto);
                bfree_guest_qv4_heartbeat("post_array_alloc");
                classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());
            } else {
                bfree_guest_qv4_heartbeat("post_array_alloc_null");
            }
            bfree_guest_qv4_heartbeat("post_array_chgproto");
        }
        bfree_guest_qv4_heartbeat(arrayIc ? "array_proto" : "array_proto_null");
        jsObjects[PropertyListProto] = memoryManager->allocate<PropertyListPrototype>();

        Heap::InternalClass *argsIc = newInternalClass(ArgumentsObject::staticVTable(), objectPrototype());
        if (argsIc) {
            argsIc = argsIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_NotEnumerable);
            argsIc = argsIc->addMember(guestBuiltinSymbolKey(this, Symbol_iterator),
                                       Attr_Data | Attr_NotEnumerable);
            classes[Class_ArgumentsObject] = argsIc->addMember(
                    guestBuiltinStringKey(this, QStringLiteral("callee")), Attr_Data | Attr_NotEnumerable);
        }
        Heap::InternalClass *strictArgsIc = newInternalClass(StrictArgumentsObject::staticVTable(), objectPrototype());
        if (strictArgsIc) {
            strictArgsIc = strictArgsIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")),
                                                    Attr_NotEnumerable);
            strictArgsIc = strictArgsIc->addMember(guestBuiltinSymbolKey(this, Symbol_iterator),
                                                     Attr_Data | Attr_NotEnumerable);
            classes[Class_StrictArgumentsObject] = strictArgsIc->addMember(
                    guestBuiltinStringKey(this, QStringLiteral("callee")),
                    Attr_Accessor | Attr_NotConfigurable | Attr_NotEnumerable);
        }
        bfree_guest_qv4_heartbeat("args_proto");
    }
#else
    ic = newInternalClass(ArrayPrototype::staticVTable(), objectPrototype());
    Q_ASSERT(ic->d()->prototype);
    ic = ic->addMember(id_length()->propertyKey(), Attr_NotConfigurable|Attr_NotEnumerable);
    Q_ASSERT(ic->d()->prototype);
    jsObjects[ArrayProto] = memoryManager->allocObject<ArrayPrototype>(ic->d());
    classes[Class_ArrayObject] = ic->changePrototype(arrayPrototype()->d());
    jsObjects[PropertyListProto] = memoryManager->allocate<PropertyListPrototype>();

    Scoped<InternalClass> argsClass(scope);
    argsClass = newInternalClass(ArgumentsObject::staticVTable(), objectPrototype());
    argsClass = argsClass->addMember(id_length()->propertyKey(), Attr_NotEnumerable);
    argsClass = argsClass->addMember(symbol_iterator()->propertyKey(), Attr_Data|Attr_NotEnumerable);
    classes[Class_ArgumentsObject] = argsClass->addMember(id_callee()->propertyKey(), Attr_Data|Attr_NotEnumerable);
    argsClass = newInternalClass(StrictArgumentsObject::staticVTable(), objectPrototype());
    argsClass = argsClass->addMember(id_length()->propertyKey(), Attr_NotEnumerable);
    argsClass = argsClass->addMember(symbol_iterator()->propertyKey(), Attr_Data|Attr_NotEnumerable);
    classes[Class_StrictArgumentsObject] = argsClass->addMember(id_callee()->propertyKey(), Attr_Accessor|Attr_NotConfigurable|Attr_NotEnumerable);
#endif"""

if "bfree_guest_qv4_heartbeat(arrayIc ? \"array_proto\"" in t:
    if "Heap::ArrayObject *arrayProto = memoryManager->allocateObject<ArrayPrototype>" in t:
        print("[array_proto_guest] already applied (allocateObject)")
    elif "allocObject<ArrayPrototype>(arrayIc)" in t or "allocateObject<ArrayPrototype>(arrayIc)" in t:
        old_alloc = """            bfree_guest_qv4_heartbeat("post_array_add");
            jsObjects[ArrayProto] = memoryManager->allocObject<ArrayPrototype>(arrayIc);
            bfree_guest_qv4_heartbeat("post_array_alloc");
            classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());
            bfree_guest_qv4_heartbeat("post_array_chgproto");"""
        old_alloc2 = """            bfree_guest_qv4_heartbeat("post_array_add");
            jsObjects[ArrayProto] = memoryManager->allocateObject<ArrayPrototype>(arrayIc);
            bfree_guest_qv4_heartbeat(jsObjects[ArrayProto] ? "post_array_alloc" : "post_array_alloc_null");
            if (jsObjects[ArrayProto])
                classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());
            bfree_guest_qv4_heartbeat("post_array_chgproto");"""
        new_alloc = """            bfree_guest_qv4_heartbeat("post_array_add");
            if (Heap::ArrayObject *arrayProto = memoryManager->allocateObject<ArrayPrototype>(arrayIc)) {
                jsObjects[ArrayProto] = Value::fromHeapObject(arrayProto);
                bfree_guest_qv4_heartbeat("post_array_alloc");
                classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());
            } else {
                bfree_guest_qv4_heartbeat("post_array_alloc_null");
            }
            bfree_guest_qv4_heartbeat("post_array_chgproto");"""
        if old_alloc in t:
            p.write_text(t.replace(old_alloc, new_alloc, 1))
            print("[array_proto_guest] ok (allocObject -> allocateObject)")
        elif old_alloc2 in t:
            p.write_text(t.replace(old_alloc2, new_alloc, 1))
            print("[array_proto_guest] ok (value check fix)")
        else:
            raise SystemExit("[array_proto_guest] alloc upgrade anchor missing")
    elif "post_array_add" in t:
        print("[array_proto_guest] already applied (with heartbeats)")
    else:
        old_hb = """            arrayIc = arrayIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")),
                                         Attr_NotConfigurable | Attr_NotEnumerable);
            jsObjects[ArrayProto] = memoryManager->allocObject<ArrayPrototype>(arrayIc);
            classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());"""
        new_hb = """            arrayIc = arrayIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")),
                                         Attr_NotConfigurable | Attr_NotEnumerable);
            bfree_guest_qv4_heartbeat("post_array_add");
            if (Heap::ArrayObject *arrayProto = memoryManager->allocateObject<ArrayPrototype>(arrayIc)) {
                jsObjects[ArrayProto] = Value::fromHeapObject(arrayProto);
                bfree_guest_qv4_heartbeat("post_array_alloc");
                classes[Class_ArrayObject] = arrayIc->changePrototype(arrayPrototype()->d());
            } else {
                bfree_guest_qv4_heartbeat("post_array_alloc_null");
            }
            bfree_guest_qv4_heartbeat("post_array_chgproto");"""
        if old_hb in t:
            p.write_text(t.replace(old_hb, new_hb, 1))
            print("[array_proto_guest] ok (heartbeats)")
        else:
            raise SystemExit("[array_proto_guest] heartbeat anchor missing")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[array_proto_guest] ok")
else:
    raise SystemExit("[array_proto_guest] anchor missing")
PY
