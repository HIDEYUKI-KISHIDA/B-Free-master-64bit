#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

early_old = """    bfree_guest_qv4_heartbeat("class_symbol");
    bfree_guest_qv4_heartbeat("pre_jsstrings");"""

early_new = """    bfree_guest_qv4_heartbeat("class_symbol");
    {
        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(numberIc ? "number_ic_ok" : "number_ic_null");
        if (numberIc) {
            if (auto *np = memoryManager->allocateObject<NumberObject>(numberIc))
                jsObjects[NumberProto] = Value::fromHeapObject(np);
        }
        bfree_guest_qv4_heartbeat(jsObjects[NumberProto].isObject() ? "number_proto" : "number_proto_null");

        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(boolIc ? "bool_ic_ok" : "bool_ic_null");
        if (boolIc) {
            if (auto *bp = memoryManager->allocateObject<BooleanObject>(boolIc))
                jsObjects[BooleanProto] = Value::fromHeapObject(bp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[BooleanProto].isObject() ? "bool_proto" : "bool_proto_null");

        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(dateIc ? "date_ic_ok" : "date_ic_null");
        if (dateIc) {
            if (auto *dp = memoryManager->allocateObject<DateObject>(dateIc))
                jsObjects[DateProto] = Value::fromHeapObject(dp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[DateProto].isObject() ? "date_proto" : "date_proto_null");
    }
    bfree_guest_qv4_heartbeat("pre_jsstrings");"""

post_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    {
        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(numberIc ? "number_ic_ok" : "number_ic_null");
        if (numberIc) {
            if (auto *np = memoryManager->allocateObject<NumberPrototype>(numberIc))
                jsObjects[NumberProto] = Value::fromHeapObject(np);
        }
        bfree_guest_qv4_heartbeat(jsObjects[NumberProto].isObject() ? "number_proto" : "number_proto_null");

        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(boolIc ? "bool_ic_ok" : "bool_ic_null");
        if (boolIc) {
            if (auto *bp = memoryManager->allocateObject<BooleanPrototype>(boolIc))
                jsObjects[BooleanProto] = Value::fromHeapObject(bp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[BooleanProto].isObject() ? "bool_proto" : "bool_proto_null");

        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(dateIc ? "date_ic_ok" : "date_ic_null");
        if (dateIc) {
            if (auto *dp = memoryManager->allocateObject<DatePrototype>(dateIc))
                jsObjects[DateProto] = Value::fromHeapObject(dp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[DateProto].isObject() ? "date_proto" : "date_proto_null");
    }
#else"""

post_old2 = """#if defined(BFREE_GUEST_FIXED_STACK)
    {
        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(numberIc ? "number_ic_ok" : "number_ic_null");
        if (numberIc) {
            if (auto *np = memoryManager->allocateObject<NumberPrototype>(numberIc))
                jsObjects[NumberProto] = Value::fromHeapObject(np);
        }
        bfree_guest_qv4_heartbeat(jsObjects[NumberProto].isObject() ? "number_proto" : "number_proto_null");

        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(boolIc ? "bool_ic_ok" : "bool_ic_null");
        if (boolIc) {
            if (auto *bp = memoryManager->allocateObject<BooleanPrototype>(boolIc))
                jsObjects[BooleanProto] = Value::fromHeapObject(bp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[BooleanProto].isObject() ? "bool_proto" : "bool_proto_null");

        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(dateIc ? "date_ic_ok" : "date_ic_null");
        if (dateIc) {
            if (auto *dp = memoryManager->allocateObject<DatePrototype>(dateIc))
                jsObjects[DateProto] = Value::fromHeapObject(dp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[DateProto].isObject() ? "date_proto" : "date_proto_null");
    }
#else"""

post_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("protos_skip_post_root");
#else"""

changed = False
if early_new in t:
    print("[protos_early] early block already applied")
elif early_old in t:
    t = t.replace(early_old, early_new, 1)
    changed = True
    print("[protos_early] ok (early alloc)")
else:
    raise SystemExit("[protos_early] early anchor missing")

if post_new.split("#else")[0] in t and "protos_skip_post_root" in t:
    print("[protos_early] post-root skip already applied")
elif post_old2 in t:
    t = t.replace(post_old2, post_new, 1)
    changed = True
    print("[protos_early] ok (post-root skip v2)")
elif post_old in t:
    t = t.replace(post_old, post_new, 1)
    changed = True
    print("[protos_early] ok (post-root skip)")
else:
    raise SystemExit("[protos_early] post-root anchor missing")

if changed:
    p.write_text(t)
# fix mistaken init=false overload if present
t = p.read_text()
t2 = t.replace("allocateObject<NumberPrototype>(numberIc, /*init =*/ false)",
               "allocateObject<NumberPrototype>(numberIc)")
t2 = t2.replace("allocateObject<BooleanPrototype>(boolIc, /*init =*/ false)",
                "allocateObject<BooleanPrototype>(boolIc)")
t2 = t2.replace("allocateObject<DatePrototype>(dateIc, /*init =*/ false)",
                "allocateObject<DatePrototype>(dateIc)")
if t2 != t:
    p.write_text(t2)
    print("[protos_early] fixed init=false overload")
print("[protos_early] done")
PY
