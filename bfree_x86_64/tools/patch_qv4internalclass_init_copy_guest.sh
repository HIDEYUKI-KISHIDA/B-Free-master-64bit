#!/usr/bin/env bash
# Guest InternalClass::init(other): copy propertyTable/nameMap/propertyData from parent.
# Guest copy ctor: MemberData pool fallback when allocate fails.
# Guest addMemberImpl: do not re-init maps (init(other) already copied).
set -eu
python3 - <<'PY'
from pathlib import Path

cp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = cp.read_text()

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
#else
        new (&nameMap) SharedInternalClassData<PropertyKey>(eng);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(eng);
#endif
    } else {
        new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    }"""

init_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
#endif
    if (other->size > 0 && other->propertyTable.d)
        new (&propertyTable) PropertyHash(other->propertyTable);
    else
        bfree_guest_property_hash_init(&propertyTable);
    nameMap.d = nullptr;
    propertyData.d = nullptr;
    if (other->size > 0 && other->nameMap.d)
        new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
    else
        bfree_guest_name_map_init(&nameMap, eng);
    if (other->size > 0 && other->propertyData.d)
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    else
        bfree_guest_property_data_init(&propertyData, eng);"""

init_new_alt = """#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
#endif
    if (other->propertyTable.d)
        new (&propertyTable) PropertyHash(other->propertyTable);
    else
        bfree_guest_property_hash_init(&propertyTable);
    nameMap.d = nullptr;
    propertyData.d = nullptr;
    if (other->nameMap.d)
        new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
    else
        bfree_guest_name_map_init(&nameMap, eng);
    if (other->propertyData.d)
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);
    else
        bfree_guest_property_data_init(&propertyData, eng);"""

init_new_alt2 = """#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
#endif
    new (&propertyTable) PropertyHash(other->propertyTable);
    nameMap.d = nullptr;
    propertyData.d = nullptr;
    new (&nameMap) SharedInternalClassData<PropertyKey>(other->nameMap);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(other->propertyData);"""

if init_new in t:
    print("[init_copy] init(other) already patched")
elif init_new_alt in t:
    t = t.replace(init_new_alt, init_new, 1)
    print("[init_copy] ok (init other copy upgrade null-safe)")
elif init_new_alt2 in t:
    t = t.replace(init_new_alt2, init_new, 1)
    print("[init_copy] ok (init other copy upgrade)")
elif init_old in t:
    t = t.replace(init_old, init_new, 1)
    print("[init_copy] ok (init other copy)")
else:
    raise SystemExit("[init_copy] init(other) anchor missing")

add_old = """    bfree_guest_qv4_heartbeat("add_impl_post_entry");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_name_map_add(&newClass->nameMap, newClass->size, identifier, eng);"""

add_new = """    bfree_guest_qv4_heartbeat("add_impl_post_entry");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_name_map_init(&newClass->nameMap, eng);
    bfree_guest_property_data_init(&newClass->propertyData, eng);
    bfree_guest_name_map_add(&newClass->nameMap, newClass->size, identifier, eng);"""

if add_new in t:
    print("[init_copy] addMemberImpl already patched")
elif add_old in t:
    t = t.replace(add_old, add_new, 1)
    print("[init_copy] ok (addMember re-init restored)")
else:
    raise SystemExit("[init_copy] addMember anchor missing")

pt_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_hash_init(&newClass->propertyTable);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };"""

pt_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    if (!newClass->propertyTable.d)
        bfree_guest_property_hash_init(&newClass->propertyTable);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };"""

if pt_new in t:
    print("[init_copy] propertyTable already patched")
elif pt_old in t:
    t = t.replace(pt_old, pt_new, 1)
    print("[init_copy] ok (propertyTable keep copy)")
else:
    print("[init_copy] propertyTable anchor missing (skipped)")

fwd = """#if defined(BFREE_GUEST_FIXED_STACK)
static void bfree_guest_memberdata_ptr_set(WriteBarrier::Pointer<Heap::MemberData> &p, Heap::MemberData *m);
static Heap::MemberData *bfree_guest_property_key_grow_pool(uint allocSlots, uint sizeSlots);
static Heap::MemberData *bfree_guest_property_key_copy_pool(uint allocSlots, uint sizeSlots,
                                                            const Heap::MemberData *src);
#endif

"""
if "bfree_guest_property_key_copy_pool(uint allocSlots" not in t.split("SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)")[0]:
    anchor = "SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)"
    if anchor not in t:
        raise SystemExit("[init_copy] copy ctor anchor missing")
    t = t.replace(anchor, fwd + anchor, 1)
    print("[init_copy] ok (forward decls)")

if "bfree_guest_property_key_copy_pool" not in t:
    anchor = "static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)"
    if anchor not in t:
        raise SystemExit("[init_copy] name_map_add anchor missing")
    copy_helper = """
static Heap::MemberData *bfree_guest_property_key_copy_pool(uint allocSlots, uint sizeSlots,
                                                            const Heap::MemberData *src)
{
    Heap::MemberData *m = bfree_guest_property_key_grow_pool(allocSlots, sizeSlots);
    if (m && src && sizeSlots) {
        for (uint i = 0; i < sizeSlots; ++i)
            m->values.values[i].rawValueRef() = src->values.values[i].rawValue();
    }
    return m;
}

"""
    t = t.replace(anchor, copy_helper + anchor, 1)
    print("[init_copy] ok (copy pool helper)")

copy_ctor_old = """SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)
    : refcount(1),
      engine(other.engine)
{
    if (other.alloc()) {
        const uint s = other.size();
        data.set(engine, MemberData::allocate(engine, other.alloc(), other.data));
        setSize(s);
    }
}"""

copy_ctor_new = """SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)
    : refcount(1),
      engine(other.engine)
{
    if (other.alloc()) {
        const uint s = other.size();
        Heap::MemberData *m = MemberData::allocate(engine, other.alloc(), other.data);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!m)
            m = bfree_guest_property_key_copy_pool(other.alloc(), s, other.data);
        if (m)
            bfree_guest_memberdata_ptr_set(data, m);
        else
            data.set(engine, nullptr);
#else
        data.set(engine, m);
#endif
        setSize(s);
    }
}"""

if copy_ctor_new in t:
    print("[init_copy] copy ctor already patched")
elif copy_ctor_old in t:
    t = t.replace(copy_ctor_old, copy_ctor_new, 1)
    print("[init_copy] ok (copy ctor)")
else:
    raise SystemExit("[init_copy] copy ctor body anchor missing")

pos_ctor_old = """SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other,
                                                                            uint pos, PropertyKey value)
    : refcount(1),
      engine(other.engine)
{
    data.set(engine, MemberData::allocate(engine, other.alloc(), nullptr));
    memcpy(data, other.data, sizeof(Heap::MemberData) - sizeof(Value) + pos*sizeof(Value));
    data->values.size = pos + 1;
    data->values.set(engine, pos, Value::fromReturnedValue(value.id()));
}"""

pos_ctor_new = """SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other,
                                                                            uint pos, PropertyKey value)
    : refcount(1),
      engine(other.engine)
{
    Heap::MemberData *m = MemberData::allocate(engine, other.alloc(), nullptr);
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m)
        m = bfree_guest_property_key_copy_pool(other.alloc(), other.size(), other.data);
    if (m) {
        bfree_guest_memberdata_ptr_set(data, m);
        data->values.size = pos + 1;
        data->values.set(engine, pos, Value::fromReturnedValue(value.id()));
        return;
    }
#endif
    data.set(engine, m);
    memcpy(data, other.data, sizeof(Heap::MemberData) - sizeof(Value) + pos*sizeof(Value));
    data->values.size = pos + 1;
    data->values.set(engine, pos, Value::fromReturnedValue(value.id()));
}"""

if pos_ctor_new in t:
    print("[init_copy] pos ctor already patched")
elif pos_ctor_old in t:
    t = t.replace(pos_ctor_old, pos_ctor_new, 1)
    print("[init_copy] ok (pos ctor)")
else:
    print("[init_copy] pos ctor anchor missing (skipped)")

cp.write_text(t)
print("[init_copy] done")
PY
