from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()
t2 = t.replace("allocateObject<NumberPrototype>(numberIc, /*init =*/ false)",
               "allocateObject<NumberPrototype>(numberIc)")
t2 = t2.replace("allocateObject<BooleanPrototype>(boolIc, /*init =*/ false)",
                "allocateObject<BooleanPrototype>(boolIc)")
t2 = t2.replace("allocateObject<DatePrototype>(dateIc, /*init =*/ false)",
                "allocateObject<DatePrototype>(dateIc)")
if t2 == t:
    print("[fix] no change")
else:
    p.write_text(t2)
    print("[fix] ok")
