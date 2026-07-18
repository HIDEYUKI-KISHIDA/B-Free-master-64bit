#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

old = """    Scope scope(this);
    Scoped<InternalClass> ic(scope);
    ic = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(ic->d());
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *classObject = ic->changePrototype(objectPrototype()->d());
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");
    if (classObject)
        classes[Class_QmlContextWrapper] = classObject->changeVTable(QV4::QQmlContextWrapper::staticVTable());
    bfree_guest_qv4_heartbeat("class_qmlctx");
#else
    classes[Class_Object] = ic->changePrototype(objectPrototype()->d());
    bfree_guest_qv4_heartbeat("class_object");
    classes[Class_QmlContextWrapper] = classes[Class_Object]->changeVTable(QV4::QQmlContextWrapper::staticVTable());
#endif"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *objectClass = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    bfree_guest_qv4_heartbeat("class_object_vt");
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(objectClass);
    Heap::InternalClass *classObject = objectClass ? objectClass->changePrototype(objectPrototype()->d()) : nullptr;
    classes[Class_Object] = classObject;
    bfree_guest_qv4_heartbeat(classObject ? "class_object" : "class_object_null");
    if (classObject)
        classes[Class_QmlContextWrapper] = classObject->changeVTable(QV4::QQmlContextWrapper::staticVTable());
    bfree_guest_qv4_heartbeat("class_qmlctx");
    Scope scope(this);
    Scoped<InternalClass> ic(scope);
    ic = objectClass;
#else
    Scope scope(this);
    Scoped<InternalClass> ic(scope);
    ic = classes[Class_Empty]->changeVTable(QV4::Object::staticVTable());
    jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(ic->d());
    classes[Class_Object] = ic->changePrototype(objectPrototype()->d());
    bfree_guest_qv4_heartbeat("class_object");
    classes[Class_QmlContextWrapper] = classes[Class_Object]->changeVTable(QV4::QQmlContextWrapper::staticVTable());
#endif"""

if new in text:
    print("[patch_classobject_heap] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[patch_classobject_heap] ok")
else:
    raise SystemExit("qv4engine object-class anchor missing")
PY
