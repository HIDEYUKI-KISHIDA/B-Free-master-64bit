from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = eng.read_text()

old = """        bfree_guest_qv4_heartbeat(arrayIc ? "array_proto" : "array_proto_null");
        jsObjects[PropertyListProto] = memoryManager->allocate<PropertyListPrototype>();

        Heap::InternalClass *argsIc = newInternalClass(ArgumentsObject::staticVTable(), objectPrototype());"""

new = """        bfree_guest_qv4_heartbeat(arrayIc ? "array_proto" : "array_proto_null");
        {
            Heap::Object *plp = nullptr;
            if (Heap::InternalClass *plIc = newInternalClass(PropertyListPrototype::staticVTable(), objectPrototype()))
                plp = memoryManager->allocateObject<Object>(plIc);
            if (plp)
                jsObjects[PropertyListProto] = plp;
            bfree_guest_qv4_heartbeat(plp ? "propertylist_proto" : "propertylist_proto_null");
        }

        Heap::InternalClass *argsIc = newInternalClass(ArgumentsObject::staticVTable(), objectPrototype());"""

bad = """            Heap::PropertyListPrototype *plp = nullptr;
            if (Heap::InternalClass *plIc = newInternalClass(PropertyListPrototype::staticVTable(), objectPrototype()))
                plp = memoryManager->allocateObject<PropertyListPrototype>(plIc);"""
good = """            Heap::Object *plp = nullptr;
            if (Heap::InternalClass *plIc = newInternalClass(PropertyListPrototype::staticVTable(), objectPrototype()))
                plp = memoryManager->allocateObject<Object>(plIc);"""
if bad in t:
    t = t.replace(bad, good, 1)
    eng.write_text(t)
    print("[v210] fixed PropertyListPrototype heap type")
elif "allocateObject<Object>(plIc)" in t and "propertylist_proto" in t:
    print("[v210] PropertyListProto guest alloc already patched")
elif old not in t:
    raise SystemExit("[v210] PropertyListProto anchor missing")
else:
    t = t.replace(old, new, 1)
    eng.write_text(t)
    print("[v210] PropertyListProto allocateObject guest path")

ic = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
ti = ic.read_text()

set_old = """    protoId = other->protoId;
    if (eng)
        internalClass.set(eng, this);
    return;
#endif"""

set_new = """    protoId = other->protoId;
    if (eng) {
        *reinterpret_cast<Heap::InternalClass **>(&internalClass) = this;
    }
    return;
#endif"""

if set_new.split("reinterpret_cast")[0] in ti:
    print("[v210] init(other) internalClass guest assign already patched")
elif set_old not in ti:
    raise SystemExit("[v210] init(other) internalClass anchor missing")
else:
    ti = ti.replace(set_old, set_new, 1)
    ic.write_text(ti)
    print("[v210] init(other) bypass WriteBarrier internalClass.set on guest")

print("[v210] done")
