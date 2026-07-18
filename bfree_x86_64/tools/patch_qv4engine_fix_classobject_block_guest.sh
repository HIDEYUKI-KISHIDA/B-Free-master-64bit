#!/usr/bin/env bash
# Deduplicate corrupted Class_Object guest/host blocks in qv4engine.cpp.
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

start = text.find("#if defined(BFREE_GUEST_FIXED_STACK)\n    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());")
end = text.find("\n#if defined(BFREE_GUEST_FIXED_STACK)\n    Heap::InternalClass *stringObjClass = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());")
if start < 0 or end < 0:
    raise SystemExit("[fix_classobject_block] markers missing")

good = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    bfree_guest_qv4_heartbeat("class_object_vt");
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(objectClass);
    Heap::InternalClass *classObject = objectClass ? objectClass->changePrototype(objectPrototype()->d()) : nullptr;
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");
    if (classObject)
        classes[Class_QmlContextWrapper] = classObject->changeVTable(QV4::QQmlContextWrapper::staticVTable());
    bfree_guest_qv4_heartbeat("class_qmlctx");
#else
    Scope scope(this);
    Scoped<InternalClass> ic(scope);
    ic = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(ic->d());
    classes[Class_Object] = ic->changePrototype(objectPrototype()->d());
    bfree_guest_qv4_heartbeat("class_object");
    classes[Class_QmlContextWrapper] = classes[Class_Object]->changeVTable(QV4::QQmlContextWrapper::staticVTable());
#endif"""

if text[start:end] == good:
    print("[fix_classobject_block] already clean")
else:
    path.write_text(text[:start] + good + text[end:])
    print("[fix_classobject_block] ok")
PY
