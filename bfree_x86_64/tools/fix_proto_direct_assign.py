from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

pairs = [
    ("jsObjects[NumberProto] = Value::fromHeapObject(np);",
     "jsObjects[NumberProto] = np;"),
    ("jsObjects[BooleanProto] = Value::fromHeapObject(bp);",
     "jsObjects[BooleanProto] = bp;"),
    ("jsObjects[DateProto] = Value::fromHeapObject(dp);",
     "jsObjects[DateProto] = dp;"),
]
for a, b in pairs:
    t = t.replace(a, b)
p.write_text(t)
print("[fix] direct jsObjects proto assign")
