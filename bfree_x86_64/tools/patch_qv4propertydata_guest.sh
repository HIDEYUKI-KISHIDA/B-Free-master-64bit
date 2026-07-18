#!/usr/bin/env bash
# Guest PropertyAttributes: inline grow + add bypass (no memoryManager/new[]).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

grow_old = """void SharedInternalClassDataPrivate<PropertyAttributes>::grow() {
    uint alloc;
    if (!m_alloc) {
        alloc = NumAttributesInPointer;
        m_engine->memoryManager->changeUnmanagedHeapSizeUsage(alloc * sizeof(PropertyAttributes));
    } else {"""

grow_new = """void SharedInternalClassDataPrivate<PropertyAttributes>::grow() {
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m_alloc) {
        m_alloc = NumAttributesInPointer;
        return;
    }
    if (m_alloc < NumAttributesInPointer)
        m_alloc = NumAttributesInPointer;
    return;
#endif
    uint alloc;
    if (!m_alloc) {
        alloc = NumAttributesInPointer;
        m_engine->memoryManager->changeUnmanagedHeapSizeUsage(alloc * sizeof(PropertyAttributes));
    } else {"""

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static void bfree_guest_property_data_add(SharedInternalClassData<PropertyAttributes> *map, uint pos,
                                          PropertyAttributes val, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    if (!map->d)
        bfree_guest_property_data_init(map, eng);
    if (!map->d)
        return;
    auto *pd = map->d;
    if (pos == pd->size()) {
        if (pos >= pd->alloc())
            pd->grow();
        pd->setSize(pd->size() + 1);
        pd->set(pos, val);
    }
}
#endif
"""

if "bfree_guest_property_data_add" not in t:
    anchor = "void SharedInternalClassDataPrivate<PropertyAttributes>::grow()"
    if anchor not in t:
        raise SystemExit("[propertydata_guest] grow anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[propertydata_guest] ok (helper)")

if grow_new in t:
    print("[propertydata_guest] grow already patched")
elif grow_old in t:
    t = t.replace(grow_old, grow_new, 1)
    print("[propertydata_guest] ok (grow)")
else:
    raise SystemExit("[propertydata_guest] grow anchor missing")

add_old = """    bfree_guest_qv4_heartbeat("add_impl_post_namemap");
    newClass->propertyData.add(newClass->size, data);
    bfree_guest_qv4_heartbeat("add_impl_post_pdata");"""

add_new = """    bfree_guest_qv4_heartbeat("add_impl_post_namemap");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_data_add(&newClass->propertyData, newClass->size, data, eng);
#else
    newClass->propertyData.add(newClass->size, data);
#endif
    bfree_guest_qv4_heartbeat("add_impl_post_pdata");"""

if add_new in t:
    print("[propertydata_guest] addMemberImpl already patched")
elif add_old in t:
    t = t.replace(add_old, add_new, 1)
    print("[propertydata_guest] ok (addMemberImpl)")
else:
    raise SystemExit("[propertydata_guest] addMemberImpl anchor missing")

p.write_text(t)
print("[propertydata_guest] done")
PY
