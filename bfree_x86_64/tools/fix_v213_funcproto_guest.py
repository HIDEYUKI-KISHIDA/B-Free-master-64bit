from pathlib import Path

icp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
ti = icp.read_text()

# Revert v212 refcount sharing; deep-copy property hash entries only.
init_v212 = """#if defined(BFREE_GUEST_FIXED_STACK)
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

init_v213 = """#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
    bfree_guest_property_hash_init(&propertyTable);
    if (other->propertyTable.d) {
        for (int i = 0; i < other->propertyTable.d->alloc; ++i) {
            const PropertyHash::Entry &e = other->propertyTable.d->entries[i];
            if (e.identifier.isValid() && e.index < static_cast<unsigned>(other->size))
                propertyTable.addEntry(e, other->size);
        }
    }
#else
    new (&propertyTable) PropertyHash();
#endif
    if (eng) {
#if defined(BFREE_GUEST_FIXED_STACK)
        nameMap.d = nullptr;
        propertyData.d = nullptr;
        bfree_guest_name_map_init(&nameMap, eng);
        bfree_guest_property_data_init(&propertyData, eng);
        if (other->propertyData.d) {
            const uint n = other->propertyData.d->size();
            for (uint i = 0; i < n && i < static_cast<unsigned>(other->size); ++i)
                bfree_guest_property_data_add(&propertyData, i, other->propertyData.d->at(i), eng);
        }
        if (other->nameMap.d) {
            const uint n = other->nameMap.d->size();
            for (uint i = 0; i < n && i < static_cast<unsigned>(other->size); ++i)
                bfree_guest_name_map_add(&nameMap, i, other->nameMap.d->at(i), eng);
        }
#else"""

init_empty = """#if defined(BFREE_GUEST_FIXED_STACK)
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

if init_v212 in ti:
    ti = ti.replace(init_v212, init_v213, 1)
    print("[v213] init(other) deep-copies property maps")
elif init_v213.split("propertyTable.addEntry")[0] in ti:
    print("[v213] init(other) deep-copy already patched")
elif init_empty in ti:
    print("[v213] init(other) at empty guest maps (v214 ok)")
else:
    raise SystemExit("[v213] init(other) anchor missing")

icp.write_text(ti)

engp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = engp.read_text()

skip_old = """    bfree_guest_qv4_heartbeat(funcClassIc ? "func_change_member_enter" : "func_change_member_null");
    if (funcClassIc)
        funcClassIc = funcClassIc->changeMember(guestBuiltinStringKey(this, QStringLiteral("prototype")), Attr_NotConfigurable|Attr_NotEnumerable);
    bfree_guest_qv4_heartbeat("func_change_member_ok");"""

skip_new = """    bfree_guest_qv4_heartbeat(funcClassIc ? "func_change_member_enter" : "func_change_member_null");
    bfree_guest_qv4_heartbeat("func_change_member_skip");
    bfree_guest_qv4_heartbeat("func_change_member_ok");"""

if "func_change_member_skip" in te:
    print("[v213] changeMember skip already patched")
elif skip_old not in te:
    raise SystemExit("[v213] changeMember skip anchor missing")
else:
    te = te.replace(skip_old, skip_new, 1)
    print("[v213] skip func prototype changeMember on guest")

engp.write_text(te)
print("[v213] done")
