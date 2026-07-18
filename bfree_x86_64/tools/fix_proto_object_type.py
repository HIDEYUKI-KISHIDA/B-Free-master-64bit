from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

repls = [
    ("allocateObject<NumberPrototype>(numberIc)", "allocateObject<NumberObject>(numberIc)"),
    ("allocateObject<BooleanPrototype>(boolIc)", "allocateObject<BooleanObject>(boolIc)"),
    ("allocateObject<DatePrototype>(dateIc)", "allocateObject<DateObject>(dateIc)"),
]
for a, b in repls:
    t = t.replace(a, b)

ins = """        bfree_guest_qv4_heartbeat(numberIc ? "number_ic_ok" : "number_ic_null");
"""
if "number_ic_ok" not in t.split("class_symbol")[1].split("pre_jsstrings")[0]:
    t = t.replace(
        "        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());\n        if (numberIc)",
        "        Heap::InternalClass *numberIc = newInternalClass(QV4::NumberObject::staticVTable(), objectPrototype());\n"
        + ins +
        "        if (numberIc)",
        1,
    )
    t = t.replace(
        "        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());\n        if (boolIc)",
        "        Heap::InternalClass *boolIc = newInternalClass(QV4::BooleanObject::staticVTable(), objectPrototype());\n"
        "        bfree_guest_qv4_heartbeat(boolIc ? \"bool_ic_ok\" : \"bool_ic_null\");\n"
        "        if (boolIc)",
        1,
    )
    t = t.replace(
        "        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());\n        if (dateIc)",
        "        Heap::InternalClass *dateIc = newInternalClass(QV4::DateObject::staticVTable(), objectPrototype());\n"
        "        bfree_guest_qv4_heartbeat(dateIc ? \"date_ic_ok\" : \"date_ic_null\");\n"
        "        if (dateIc)",
        1,
    )

p.write_text(t)
print("[fix] object proto alloc types")
