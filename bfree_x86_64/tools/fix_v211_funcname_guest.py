from pathlib import Path

icp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
ti = icp.read_text()

obj_cm_old = """void InternalClass::changeMember(QV4::Object *object, PropertyKey id, PropertyAttributes data, InternalClassEntry *entry)
{
    Q_ASSERT(id.isStringOrSymbol());

    Heap::InternalClass *oldClass = object->internalClass();
    Heap::InternalClass *newClass = oldClass->changeMember(id, data, entry);
    object->setInternalClass(newClass);
}"""

obj_cm_new = """void InternalClass::changeMember(QV4::Object *object, PropertyKey id, PropertyAttributes data, InternalClassEntry *entry)
{
    Q_ASSERT(id.isStringOrSymbol());
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!object)
        return;
    Heap::InternalClass *oldClass = object->internalClass();
    if (!oldClass)
        return;
    Heap::InternalClass *newClass = oldClass->changeMember(id, data, entry);
    if (newClass)
        object->setInternalClass(newClass);
#else
    Heap::InternalClass *oldClass = object->internalClass();
    Heap::InternalClass *newClass = oldClass->changeMember(id, data, entry);
    object->setInternalClass(newClass);
#endif
}"""

cm_guard_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!propertyData.d && eng)
        bfree_guest_property_data_init(&propertyData, eng);
    PropertyHash::Entry *e = findEntry(identifier);"""

cm_guard_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!propertyTable.d)
        bfree_guest_property_hash_init(&propertyTable);
    if (!propertyData.d && eng)
        bfree_guest_property_data_init(&propertyData, eng);
    if (!eng || !propertyTable.d || !propertyData.d)
        return this;
    PropertyHash::Entry *e = findEntry(identifier);"""

if obj_cm_new.split("if (!object)")[0] in ti:
    print("[v211] Object changeMember guest guard already patched")
elif obj_cm_old not in ti:
    raise SystemExit("[v211] Object changeMember anchor missing")
else:
    ti = ti.replace(obj_cm_old, obj_cm_new, 1)
    print("[v211] Object changeMember guest guard")

if cm_guard_new.split("!propertyTable.d")[0] in ti:
    print("[v211] Heap changeMember guards already patched")
elif cm_guard_old not in ti:
    raise SystemExit("[v211] Heap changeMember guard anchor missing")
else:
    ti = ti.replace(cm_guard_old, cm_guard_new, 1)
    print("[v211] Heap changeMember table/data guards")

icp.write_text(ti)

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

old = """    bfree_guest_qv4_heartbeat(funcObjIc ? "func_obj_ic_ok" : "func_obj_ic_null");
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    }
    if (funcObjIc)
        classes[Class_ArrowFunction] = funcObjIc->changeVTable(ArrowFunction::staticVTable());"""

new = """    bfree_guest_qv4_heartbeat(funcObjIc ? "func_obj_ic_ok" : "func_obj_ic_null");
    if (funcObjIc)
        classes[Class_ArrowFunction] = funcObjIc->changeVTable(ArrowFunction::staticVTable());
    bfree_guest_qv4_heartbeat(classes[Class_ArrowFunction] ? "func_arrow_skip_name_ok" : "func_arrow_skip_name_null");"""

if "func_arrow_skip_name_ok" in te:
    print("[v211] func name/length skip already patched")
elif old not in te:
    raise SystemExit("[v211] func_obj name/length anchor missing")
else:
    te = te.replace(old, new, 1)
    print("[v211] skip func_obj name/length addMember on guest")

eng.write_text(te)
print("[v211] done")
