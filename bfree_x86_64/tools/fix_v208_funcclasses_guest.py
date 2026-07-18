from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = eng.read_text()

old = """    bfree_guest_qv4_heartbeat("func_classes_enter");
    Heap::InternalClass *funcObjIc = newInternalClass(FunctionObject::staticVTable(), functionPrototype());
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
    }
    classes[Class_FunctionObject] = funcObjIc;
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    }
    if (funcObjIc)
        classes[Class_ArrowFunction] = funcObjIc->changeVTable(ArrowFunction::staticVTable());
    if (classes[Class_ArrowFunction])
        classes[Class_MemberFunction] = classes[Class_ArrowFunction]->changeVTable(MemberFunction::staticVTable());
    if (classes[Class_MemberFunction])
        classes[Class_GeneratorFunction] = classes[Class_MemberFunction]->changeVTable(GeneratorFunction::staticVTable());
    if (classes[Class_GeneratorFunction])
        classes[Class_MemberGeneratorFunction] = classes[Class_GeneratorFunction]->changeVTable(MemberGeneratorFunction::staticVTable());

    Heap::InternalClass *funcClassIc = classes[Class_MemberGeneratorFunction];
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ScriptFunction::staticVTable());
    classes[Class_ScriptFunction] = funcClassIc;
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ConstructorFunction::staticVTable());
    classes[Class_ConstructorFunction] = funcClassIc;
    if (funcClassIc)
        ic = funcClassIc;
    bfree_guest_qv4_heartbeat("func_classes_done");"""

new = """    bfree_guest_qv4_heartbeat("func_classes_enter");
    Heap::InternalClass *funcObjIc = newInternalClass(FunctionObject::staticVTable(), functionPrototype());
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_Invalid, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinSymbolKey(this, Symbol_hasInstance), Attr_Invalid, index);
    }
    classes[Class_FunctionObject] = funcObjIc;
    bfree_guest_qv4_heartbeat(funcObjIc ? "func_obj_ic_ok" : "func_obj_ic_null");
    if (funcObjIc) {
        funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("name")), Attr_ReadOnly, index);
        if (funcObjIc)
            funcObjIc = funcObjIc->addMember(guestBuiltinStringKey(this, QStringLiteral("length")), Attr_ReadOnly_ButConfigurable, index);
    }
    if (funcObjIc)
        classes[Class_ArrowFunction] = funcObjIc->changeVTable(ArrowFunction::staticVTable());
    bfree_guest_qv4_heartbeat(classes[Class_ArrowFunction] ? "func_arrow_ok" : "func_arrow_null");
    if (classes[Class_ArrowFunction])
        classes[Class_MemberFunction] = classes[Class_ArrowFunction]->changeVTable(MemberFunction::staticVTable());
    if (classes[Class_MemberFunction])
        classes[Class_GeneratorFunction] = classes[Class_MemberFunction]->changeVTable(GeneratorFunction::staticVTable());
    if (classes[Class_GeneratorFunction])
        classes[Class_MemberGeneratorFunction] = classes[Class_GeneratorFunction]->changeVTable(MemberGeneratorFunction::staticVTable());
    bfree_guest_qv4_heartbeat(classes[Class_MemberGeneratorFunction] ? "func_member_gen_ok" : "func_member_gen_null");

    Heap::InternalClass *funcClassIc = classes[Class_MemberGeneratorFunction];
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    bfree_guest_qv4_heartbeat("func_change_member_ok");
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ScriptFunction::staticVTable());
    classes[Class_ScriptFunction] = funcClassIc;
    bfree_guest_qv4_heartbeat(funcClassIc ? "func_script_ok" : "func_script_null");
    if (funcClassIc)
        funcClassIc = funcClassIc->changeVTable(ConstructorFunction::staticVTable());
    classes[Class_ConstructorFunction] = funcClassIc;
    bfree_guest_qv4_heartbeat(funcClassIc ? "func_ctor_class_ok" : "func_ctor_class_null");
    if (funcClassIc)
        ic = funcClassIc;
    bfree_guest_qv4_heartbeat("func_classes_done");"""

if "func_change_member_ok" in t:
    print("[v208] func_classes heartbeats already patched")
elif old not in t:
    raise SystemExit("[v208] func_classes anchor missing")
else:
    t = t.replace(old, new, 1)
    print("[v208] func_classes step heartbeats")

eng.write_text(t)
print("[v208] done")
