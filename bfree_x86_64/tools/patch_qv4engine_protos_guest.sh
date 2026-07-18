#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

old = """#endif

    jsObjects[NumberProto] = memoryManager->allocate<NumberPrototype>();
    jsObjects[BooleanProto] = memoryManager->allocate<BooleanPrototype>();
    jsObjects[DateProto] = memoryManager->allocate<DatePrototype>();

#if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)"""

new = """#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    {
        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(numberIc ? "number_ic_ok" : "number_ic_null");
        if (numberIc) {
            if (auto *np = memoryManager->allocateObject<NumberPrototype>(numberIc, /*init =*/ false))
                jsObjects[NumberProto] = Value::fromHeapObject(np);
        }
        bfree_guest_qv4_heartbeat(jsObjects[NumberProto].isObject() ? "number_proto" : "number_proto_null");

        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(boolIc ? "bool_ic_ok" : "bool_ic_null");
        if (boolIc) {
            if (auto *bp = memoryManager->allocateObject<BooleanPrototype>(boolIc, /*init =*/ false))
                jsObjects[BooleanProto] = Value::fromHeapObject(bp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[BooleanProto].isObject() ? "bool_proto" : "bool_proto_null");

        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(dateIc ? "date_ic_ok" : "date_ic_null");
        if (dateIc) {
            if (auto *dp = memoryManager->allocateObject<DatePrototype>(dateIc, /*init =*/ false))
                jsObjects[DateProto] = Value::fromHeapObject(dp);
        }
        bfree_guest_qv4_heartbeat(jsObjects[DateProto].isObject() ? "date_proto" : "date_proto_null");
    }
#else
    jsObjects[NumberProto] = memoryManager->allocate<NumberPrototype>();
    jsObjects[BooleanProto] = memoryManager->allocate<BooleanPrototype>();
    jsObjects[DateProto] = memoryManager->allocate<DatePrototype>();
#endif

#if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)"""

if new in t:
    print("[protos_guest] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[protos_guest] ok")
else:
    raise SystemExit("[protos_guest] anchor missing")
PY
