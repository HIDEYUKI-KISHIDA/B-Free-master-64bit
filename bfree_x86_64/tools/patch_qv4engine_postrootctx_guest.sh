#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

old = """    bfree_guest_qv4_heartbeat("root_ctx");

    ic = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly);
    classes[Class_StringObject] = ic->changePrototype(stringPrototype()->d());
    Q_ASSERT(classes[Class_StringObject]->verifyIndex(guestBuiltinStringKey(this, QStringLiteral("length")), Heap::StringObject::LengthPropertyIndex));

    classes[Class_SymbolObject] = newInternalClass(QV4::SymbolObject::staticVTable(), symbolPrototype());"""

new = """    bfree_guest_qv4_heartbeat("root_ctx");

#if defined(BFREE_GUEST_FIXED_STACK)
    {
        Heap::InternalClass *strObjIc = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
        bfree_guest_qv4_heartbeat(strObjIc ? "str_obj_ic_ok" : "str_obj_ic_null");
        if (strObjIc) {
            strObjIc = strObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly);
            if (strObjIc)
                classes[Class_StringObject] = strObjIc->changePrototype(stringPrototype()->d());
        }
        bfree_guest_qv4_heartbeat(classes[Class_StringObject] ? "class_string_object" : "class_string_object_null");
        Heap::InternalClass *symObjIc = newInternalClass(QV4::SymbolObject::staticVTable(), symbolPrototype());
        if (symObjIc)
            classes[Class_SymbolObject] = symObjIc;
        bfree_guest_qv4_heartbeat(symObjIc ? "class_symbol_object" : "class_symbol_object_null");
    }
#else
    ic = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    ic = ic->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly);
    classes[Class_StringObject] = ic->changePrototype(stringPrototype()->d());
    Q_ASSERT(classes[Class_StringObject]->verifyIndex(guestBuiltinStringKey(this, QStringLiteral("length")), Heap::StringObject::LengthPropertyIndex));

    classes[Class_SymbolObject] = newInternalClass(QV4::SymbolObject::staticVTable(), symbolPrototype());
#endif"""

if new in t:
    print("[postrootctx] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[postrootctx] ok")
else:
    raise SystemExit("[postrootctx] anchor missing")
PY
