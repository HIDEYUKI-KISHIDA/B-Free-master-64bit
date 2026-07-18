#!/usr/bin/env bash
# Guest addDummyEntry: bypass nameMap.add / propertyData.add (null d->size() PF).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

old = """static void addDummyEntry(InternalClass *newClass, PropertyHash::Entry e)
{
    // add a dummy entry, since we need two entries for accessors
    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, PropertyKey::invalid());
    newClass->propertyData.add(newClass->size, PropertyAttributes());
    ++newClass->size;
}"""

new = """static void addDummyEntry(InternalClass *newClass, PropertyHash::Entry e)
{
    // add a dummy entry, since we need two entries for accessors
#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = newClass->engine;
#endif
    newClass->propertyTable.addEntry(e, newClass->size);
#if defined(BFREE_GUEST_FIXED_STACK)
    if (eng) {
        bfree_guest_qv4_heartbeat("add_dummy_entry");
        bfree_guest_name_map_add(&newClass->nameMap, newClass->size, PropertyKey::invalid(), eng);
        bfree_guest_property_data_add(&newClass->propertyData, newClass->size, PropertyAttributes(), eng);
    }
#else
    newClass->nameMap.add(newClass->size, PropertyKey::invalid());
    newClass->propertyData.add(newClass->size, PropertyAttributes());
#endif
    ++newClass->size;
}"""

if new in t:
    print("[adddummyentry_guest] already patched")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[adddummyentry_guest] ok")
else:
    raise SystemExit("[adddummyentry_guest] anchor missing")
PY
