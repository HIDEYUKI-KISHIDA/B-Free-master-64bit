#!/usr/bin/env bash
# Guest addMemberImpl: init nameMap/propertyData AFTER addEntry (addEntry corrupts nameMap.d).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

old = """    bfree_guest_property_hash_init(&newClass->propertyTable);
    bfree_guest_name_map_init(&newClass->nameMap, eng);
    bfree_guest_property_data_init(&newClass->propertyData, eng);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    bfree_guest_qv4_heartbeat("add_impl_pre_entry");
    newClass->propertyTable.addEntry(e, newClass->size);
    bfree_guest_qv4_heartbeat("add_impl_post_entry");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_name_map_add(&newClass->nameMap, newClass->size, identifier, eng);"""

new = """    bfree_guest_property_hash_init(&newClass->propertyTable);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    bfree_guest_qv4_heartbeat("add_impl_pre_entry");
    newClass->propertyTable.addEntry(e, newClass->size);
    bfree_guest_qv4_heartbeat("add_impl_post_entry");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_name_map_init(&newClass->nameMap, eng);
    bfree_guest_property_data_init(&newClass->propertyData, eng);
    bfree_guest_name_map_add(&newClass->nameMap, newClass->size, identifier, eng);"""

if new in t:
    print("[addmember_order] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[addmember_order] ok")
else:
    raise SystemExit("[addmember_order] anchor missing")
PY
