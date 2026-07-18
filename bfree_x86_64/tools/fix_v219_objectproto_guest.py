from pathlib import Path

mm = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
ht = mm.read_text()

ic_old = """        Heap::Object *o = allocObjectWithMemberData(ObjectType::staticVTable(), ic->size);
        if (!o)
            return nullptr;
        o->internalClass.set(engine, ic);
        Q_ASSERT(o->internalClass.get() && o->vtable());"""

ic_new = """        Heap::Object *o = allocObjectWithMemberData(ObjectType::staticVTable(), ic->size);
        if (!o)
            return nullptr;
#if defined(BFREE_GUEST_FIXED_STACK)
        *reinterpret_cast<Heap::InternalClass **>(&o->internalClass) = ic;
#else
        o->internalClass.set(engine, ic);
#endif
        Q_ASSERT(o->internalClass.get() && o->vtable());"""

if ic_new.split("reinterpret_cast")[0] in ht:
    print("[v219] allocateObject internalClass guest assign already patched")
elif ic_old not in ht:
    raise SystemExit("[v219] allocateObject internalClass anchor missing")
else:
    ht = ht.replace(ic_old, ic_new, 1)
    mm.write_text(ht)
    print("[v219] allocateObject internalClass guest assign")

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

obj_old = """    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
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
    classes[Class_Object] = classObject;"""

obj_new = """    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    bfree_guest_qv4_heartbeat("class_object_vt");
    if (objectClass) {
        bfree_guest_qv4_heartbeat("object_proto_pre");
        jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(objectClass);
        bfree_guest_qv4_heartbeat(jsObjects[ObjectProto].isManaged() ? "object_proto_ok" : "object_proto_null");
    } else {
        bfree_guest_qv4_heartbeat("object_proto_no_ic");
    }
    Heap::InternalClass *classObject = objectClass && jsObjects[ObjectProto].isManaged()
            ? objectClass->changePrototype(objectPrototype()->d()) : nullptr;
    classes[Class_Object] = classObject;"""

if obj_new.split("object_proto_pre")[0] in te or "opHeap = memoryManager->allocObject<ObjectPrototype>" in te:
    print("[v219] class_object v216 path already patched")
elif obj_old not in te:
    raise SystemExit("[v219] class_object anchor missing")
else:
    te = te.replace(obj_old, obj_new, 1)
    eng.write_text(te)
    print("[v219] class_object revert to allocObject + objectPrototype()->d()")

print("[v219] done")
