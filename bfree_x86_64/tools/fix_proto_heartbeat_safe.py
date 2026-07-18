from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

replacements = [
    (
        """        if (numberIc) {
            if (auto *np = memoryManager->allocateObject<NumberObject>(numberIc))
                jsObjects[NumberProto] = np;
        }
        bfree_guest_qv4_heartbeat(jsObjects[NumberProto].isObject() ? "number_proto" : "number_proto_null");""",
        """        Heap::NumberObject *np = nullptr;
        if (numberIc)
            np = memoryManager->allocateObject<NumberObject>(numberIc);
        if (np)
            jsObjects[NumberProto] = np;
        bfree_guest_qv4_heartbeat(np ? "number_proto" : "number_proto_null");""",
    ),
    (
        """        if (boolIc) {
            if (auto *bp = memoryManager->allocateObject<BooleanObject>(boolIc))
                jsObjects[BooleanProto] = bp;
        }
        bfree_guest_qv4_heartbeat(jsObjects[BooleanProto].isObject() ? "bool_proto" : "bool_proto_null");""",
        """        Heap::BooleanObject *bp = nullptr;
        if (boolIc)
            bp = memoryManager->allocateObject<BooleanObject>(boolIc);
        if (bp)
            jsObjects[BooleanProto] = bp;
        bfree_guest_qv4_heartbeat(bp ? "bool_proto" : "bool_proto_null");""",
    ),
    (
        """        if (dateIc) {
            if (auto *dp = memoryManager->allocateObject<DateObject>(dateIc))
                jsObjects[DateProto] = dp;
        }
        bfree_guest_qv4_heartbeat(jsObjects[DateProto].isObject() ? "date_proto" : "date_proto_null");""",
        """        Heap::DateObject *dp = nullptr;
        if (dateIc)
            dp = memoryManager->allocateObject<DateObject>(dateIc);
        if (dp)
            jsObjects[DateProto] = dp;
        bfree_guest_qv4_heartbeat(dp ? "date_proto" : "date_proto_null");""",
    ),
]

for old, new in replacements:
    if old not in t:
        if new.split("\n")[0] in t:
            continue
        raise SystemExit(f"[fix] anchor missing for {new.split(chr(10))[0]}")
    t = t.replace(old, new, 1)

p.write_text(t)
print("[fix] safe proto heartbeats (no isObject on unset jsObjects)")
