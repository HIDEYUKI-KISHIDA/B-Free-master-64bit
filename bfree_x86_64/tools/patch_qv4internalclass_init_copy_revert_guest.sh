#!/usr/bin/env bash
# Revert init_copy guest changes that regressed namemap (restore v197 internalclass path).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

init_copy = """#if defined(BFREE_GUEST_FIXED_STACK)
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

init_orig = """#if defined(BFREE_GUEST_FIXED_STACK)
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

if init_copy in t:
    t = t.replace(init_copy, init_orig, 1)
    print("[init_revert] ok (init other)")
elif init_orig in t:
    print("[init_revert] init(other) already original")
else:
    print("[init_revert] init(other) anchor missing (skipped)")

pt_copy = """#if defined(BFREE_GUEST_FIXED_STACK)
    if (!newClass->propertyTable.d)
        bfree_guest_property_hash_init(&newClass->propertyTable);
#endif"""
pt_orig = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_hash_init(&newClass->propertyTable);
#endif"""
if pt_copy in t:
    t = t.replace(pt_copy, pt_orig, 1)
    print("[init_revert] ok (propertyTable)")

copy_ctor_guest = """        Heap::MemberData *m = MemberData::allocate(engine, other.alloc(), other.data);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!m)
            m = bfree_guest_property_key_copy_pool(other.alloc(), s, other.data);
        if (m)
            bfree_guest_memberdata_ptr_set(data, m);
        else
            data.set(engine, nullptr);
#else
        data.set(engine, m);
#endif"""
copy_ctor_orig = """        data.set(engine, MemberData::allocate(engine, other.alloc(), other.data));"""
if copy_ctor_guest in t:
    t = t.replace(copy_ctor_guest, copy_ctor_orig, 1)
    print("[init_revert] ok (copy ctor)")

pos_ctor_guest = """    Heap::MemberData *m = MemberData::allocate(engine, other.alloc(), nullptr);
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
    data.set(engine, m);"""
pos_ctor_orig = """    data.set(engine, MemberData::allocate(engine, other.alloc(), nullptr));"""
if pos_ctor_guest in t:
    t = t.replace(pos_ctor_guest, pos_ctor_orig, 1)
    print("[init_revert] ok (pos ctor)")

fwd = """#if defined(BFREE_GUEST_FIXED_STACK)
static void bfree_guest_memberdata_ptr_set(WriteBarrier::Pointer<Heap::MemberData> &p, Heap::MemberData *m);
static Heap::MemberData *bfree_guest_property_key_grow_pool(uint allocSlots, uint sizeSlots);
static Heap::MemberData *bfree_guest_property_key_copy_pool(uint allocSlots, uint sizeSlots,
                                                            const Heap::MemberData *src);
#endif

SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)"""
if fwd in t:
    t = t.replace(fwd, "SharedInternalClassDataPrivate<PropertyKey>::SharedInternalClassDataPrivate(const SharedInternalClassDataPrivate<PropertyKey> &other)", 1)
    print("[init_revert] ok (remove fwd decls)")

copy_pool_fn = """
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
if copy_pool_fn in t:
    t = t.replace(copy_pool_fn, "", 1)
    print("[init_revert] ok (remove copy_pool helper)")

p.write_text(t)
print("[init_revert] done")
PY
