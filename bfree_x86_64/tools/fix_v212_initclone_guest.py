from pathlib import Path

icp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
ti = icp.read_text()

init_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
    bfree_guest_property_hash_init(&propertyTable);
#else
    new (&propertyTable) PropertyHash();
#endif
    if (eng) {
#if defined(BFREE_GUEST_FIXED_STACK)
        nameMap.d = nullptr;
        propertyData.d = nullptr;
        bfree_guest_name_map_init(&nameMap, eng);
        bfree_guest_property_data_init(&propertyData, eng);
#else"""

init_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    if (other->propertyTable.d) {
        propertyTable.d = other->propertyTable.d;
        ++propertyTable.d->refCount;
    } else {
        propertyTable.d = nullptr;
        bfree_guest_property_hash_init(&propertyTable);
    }
#else
    new (&propertyTable) PropertyHash();
#endif
    if (eng) {
#if defined(BFREE_GUEST_FIXED_STACK)
        if (other->nameMap.d) {
            nameMap.d = other->nameMap.d;
            ++nameMap.d->refcount;
        } else {
            nameMap.d = nullptr;
            bfree_guest_name_map_init(&nameMap, eng);
        }
        if (other->propertyData.d) {
            propertyData.d = other->propertyData.d;
            ++propertyData.d->refcount;
        } else {
            propertyData.d = nullptr;
            bfree_guest_property_data_init(&propertyData, eng);
        }
#else"""

cm_guard_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!propertyData.d && eng)
        bfree_guest_property_data_init(&propertyData, eng);
    PropertyHash::Entry *e = findEntry(identifier);
    if (!e || e->index >= size || !propertyData.d || !eng)
        return this;"""

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
    PropertyHash::Entry *e = findEntry(identifier);
    if (!e || e->index >= size)
        return this;"""

if "other->propertyTable.d" in ti and "++propertyTable.d->refCount" in ti:
    print("[v212] init(other) clone already patched")
elif init_old not in ti:
    raise SystemExit("[v212] init(other) anchor missing")
else:
    ti = ti.replace(init_old, init_new, 1)
    print("[v212] init(other) shares parent property maps")

if cm_guard_new.split("!propertyTable.d")[0] in ti and "if (!eng || !propertyTable.d || !propertyData.d)" in ti:
    print("[v212] changeMember guards already patched")
elif cm_guard_old not in ti:
    raise SystemExit("[v212] changeMember guard anchor missing")
else:
    ti = ti.replace(cm_guard_old, cm_guard_new, 1)
    print("[v212] changeMember propertyTable guard")

icp.write_text(ti)

engp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = engp.read_text()

hb_old = """    Heap::InternalClass *funcClassIc = classes[Class_MemberGeneratorFunction];
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    bfree_guest_qv4_heartbeat("func_change_member_ok");"""

hb_new = """    Heap::InternalClass *funcClassIc = classes[Class_MemberGeneratorFunction];
    bfree_guest_qv4_heartbeat(funcClassIc ? "func_change_member_enter" : "func_change_member_null");
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    bfree_guest_qv4_heartbeat("func_change_member_ok");"""

if "func_change_member_enter" in te:
    print("[v212] changeMember heartbeat already patched")
elif hb_old not in te:
    raise SystemExit("[v212] changeMember heartbeat anchor missing")
else:
    te = te.replace(hb_old, hb_new, 1)
    print("[v212] changeMember enter heartbeat")

engp.write_text(te)
print("[v212] done")
