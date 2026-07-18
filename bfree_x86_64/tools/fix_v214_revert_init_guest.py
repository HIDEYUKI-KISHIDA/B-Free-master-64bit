from pathlib import Path

icp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
ti = icp.read_text()

init_bad = """#if defined(BFREE_GUEST_FIXED_STACK)
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

init_good = """#if defined(BFREE_GUEST_FIXED_STACK)
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

if init_good.split("bfree_guest_property_data_init")[0] in ti and "propertyTable.addEntry(e, other->size)" not in ti:
    print("[v214] init(other) already reverted")
elif init_bad not in ti:
    raise SystemExit("[v214] init(other) deep-copy anchor missing")
else:
    ti = ti.replace(init_bad, init_good, 1)
    print("[v214] reverted init(other) to empty guest maps")

icp.write_text(ti)
print("[v214] done")
