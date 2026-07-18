#!/usr/bin/env bash
# Guest PropertyHash: ensure PropertyHashData exists (addEntry CR2=0x8 when d is null).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

helper_marker = "static PropertyHashData *bfree_guest_property_hash_data(int numBits)"
if helper_marker not in text:
    insert_after = "namespace QV4 {\n\n"
    helper = r'''namespace QV4 {

#if defined(BFREE_GUEST_FIXED_STACK)
static PropertyHashData *bfree_guest_property_hash_data(int numBits)
{
    void *mem = ::operator new(sizeof(PropertyHashData), std::nothrow);
    if (mem)
        return new (mem) PropertyHashData(numBits);
    static PropertyHashData fallback(3);
    return &fallback;
}

static void bfree_guest_property_hash_init(PropertyHash *table)
{
    if (!table)
        return;
    if (!table->d)
        table->d = bfree_guest_property_hash_data(3);
}

alignas(SharedInternalClassDataPrivate<PropertyKey>)
static char g_guestNameMapPool[4096][sizeof(SharedInternalClassDataPrivate<PropertyKey>)];
static unsigned g_guestNameMapSlot = 0;

alignas(SharedInternalClassDataPrivate<PropertyAttributes>)
static char g_guestPropertyDataPool[4096][sizeof(SharedInternalClassDataPrivate<PropertyAttributes>)];
static unsigned g_guestPropertyDataSlot = 0;

static SharedInternalClassDataPrivate<PropertyKey> *bfree_guest_name_map_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyKey>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyKey>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyKey>(eng);
    if (g_guestNameMapSlot < 4096)
        return new (g_guestNameMapPool[g_guestNameMapSlot++])
                SharedInternalClassDataPrivate<PropertyKey>(eng);
    return new (g_guestNameMapPool[4095])
            SharedInternalClassDataPrivate<PropertyKey>(eng);
}

static SharedInternalClassDataPrivate<PropertyAttributes> *bfree_guest_property_data_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    if (g_guestPropertyDataSlot < 4096)
        return new (g_guestPropertyDataPool[g_guestPropertyDataSlot++])
                SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    return new (g_guestPropertyDataPool[4095])
            SharedInternalClassDataPrivate<PropertyAttributes>(eng);
}

static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
}

static void bfree_guest_property_data_init(SharedInternalClassData<PropertyAttributes> *pd, ExecutionEngine *eng)
{
    if (!pd || !eng)
        return;
    pd->d = bfree_guest_property_data_private(eng);
}
#endif

'''
    if insert_after not in text:
        raise SystemExit("[propertyhash_guest] namespace anchor missing")
    text = text.replace(insert_after, helper, 1)
    print("[propertyhash_guest] ok (helper)")
else:
    print("[propertyhash_guest] helper already present")

shared_marker = "bfree_guest_name_map_init"
if shared_marker not in text:
    old_block = """static void bfree_guest_property_hash_init(PropertyHash *table)
{
    if (!table)
        return;
    if (!table->d)
        table->d = bfree_guest_property_hash_data(3);
}
#endif"""
    new_block = old_block.replace(
        "}\n#endif",
        """}

alignas(SharedInternalClassDataPrivate<PropertyKey>)
static char g_guestNameMapPool[4096][sizeof(SharedInternalClassDataPrivate<PropertyKey>)];
static unsigned g_guestNameMapSlot = 0;

alignas(SharedInternalClassDataPrivate<PropertyAttributes>)
static char g_guestPropertyDataPool[4096][sizeof(SharedInternalClassDataPrivate<PropertyAttributes>)];
static unsigned g_guestPropertyDataSlot = 0;

static SharedInternalClassDataPrivate<PropertyKey> *bfree_guest_name_map_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyKey>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyKey>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyKey>(eng);
    if (g_guestNameMapSlot < 4096)
        return new (g_guestNameMapPool[g_guestNameMapSlot++])
                SharedInternalClassDataPrivate<PropertyKey>(eng);
    return new (g_guestNameMapPool[4095])
            SharedInternalClassDataPrivate<PropertyKey>(eng);
}

static SharedInternalClassDataPrivate<PropertyAttributes> *bfree_guest_property_data_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    if (g_guestPropertyDataSlot < 4096)
        return new (g_guestPropertyDataPool[g_guestPropertyDataSlot++])
                SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    return new (g_guestPropertyDataPool[4095])
            SharedInternalClassDataPrivate<PropertyAttributes>(eng);
}

static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
}

static void bfree_guest_property_data_init(SharedInternalClassData<PropertyAttributes> *pd, ExecutionEngine *eng)
{
    if (!pd || !eng)
        return;
    pd->d = bfree_guest_property_data_private(eng);
}
#endif""",
        1,
    )
    if old_block not in text:
        raise SystemExit("[propertyhash_guest] shared helper insert anchor missing")
    text = text.replace(old_block, new_block, 1)
    print("[propertyhash_guest] ok (shared helpers)")
else:
    print("[propertyhash_guest] shared helpers already present")

addentry_old = """void PropertyHash::addEntry(const PropertyHash::Entry &entry, int classSize)
{
    // fill up to max 50%
    bool grow = (d->alloc <= d->size*2);"""

addentry_new = """void PropertyHash::addEntry(const PropertyHash::Entry &entry, int classSize)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!d)
        d = bfree_guest_property_hash_data(3);
#endif
    // fill up to max 50%
    bool grow = (d->alloc <= d->size*2);"""

if addentry_new in text:
    print("[propertyhash_guest] addEntry already patched")
elif addentry_old in text:
    text = text.replace(addentry_old, addentry_new, 1)
    print("[propertyhash_guest] ok (addEntry)")
else:
    raise SystemExit("[propertyhash_guest] addEntry anchor missing")

init_engine_old = """    Base::init();
    new (&propertyTable) PropertyHash();
    new (&nameMap) SharedInternalClassData<PropertyKey>(engine);"""

init_engine_new = """    Base::init();
#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
    bfree_guest_property_hash_init(&propertyTable);
#else
    new (&propertyTable) PropertyHash();
#endif
    new (&nameMap) SharedInternalClassData<PropertyKey>(engine);"""

if init_engine_new in text or "bfree_guest_name_map_init(&nameMap, engine)" in text:
    print("[propertyhash_guest] init(engine) already patched")
elif init_engine_old in text:
    text = text.replace(init_engine_old, init_engine_new, 1)
    print("[propertyhash_guest] ok (init engine)")
else:
    print("[propertyhash_guest] init(engine) anchor missing (skipped)")

init_other_old = """    engine = eng;
    new (&propertyTable) PropertyHash();
    if (eng) {"""

init_other_new = """    engine = eng;
#if defined(BFREE_GUEST_FIXED_STACK)
    propertyTable.d = nullptr;
    bfree_guest_property_hash_init(&propertyTable);
#else
    new (&propertyTable) PropertyHash();
#endif
    if (eng) {"""

if init_other_new in text or "bfree_guest_name_map_init(&nameMap, eng)" in text:
    print("[propertyhash_guest] init(other) already patched")
elif init_other_old in text:
    text = text.replace(init_other_old, init_other_new, 1)
    print("[propertyhash_guest] ok (init other)")
else:
    print("[propertyhash_guest] init(other) anchor missing (skipped)")

addmember_old = """    newClass->engine = eng;
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);"""

addmember_new = """    newClass->engine = eng;
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_hash_init(&newClass->propertyTable);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);"""

if addmember_new in text:
    print("[propertyhash_guest] addMemberImpl already patched")
elif addmember_old in text:
    text = text.replace(addmember_old, addmember_new, 1)
    print("[propertyhash_guest] ok (addMemberImpl)")
else:
    print("[propertyhash_guest] addMemberImpl already patched")

init_engine_nm_old = """#endif
    new (&nameMap) SharedInternalClassData<PropertyKey>(engine);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(engine);
    new (&transitions) QVarLengthArray<Transition, 1>();

    this->engine = engine;
    vtable = QV4::InternalClass::staticVTable();"""

init_engine_nm_new = """#endif
#if defined(BFREE_GUEST_FIXED_STACK)
    nameMap.d = nullptr;
    propertyData.d = nullptr;
    bfree_guest_name_map_init(&nameMap, engine);
    bfree_guest_property_data_init(&propertyData, engine);
#else
    new (&nameMap) SharedInternalClassData<PropertyKey>(engine);
    new (&propertyData) SharedInternalClassData<PropertyAttributes>(engine);
#endif
    new (&transitions) QVarLengthArray<Transition, 1>();

    this->engine = engine;
    vtable = QV4::InternalClass::staticVTable();"""

if init_engine_nm_new in text:
    print("[propertyhash_guest] init(engine) nameMap already patched")
elif init_engine_nm_old in text:
    text = text.replace(init_engine_nm_old, init_engine_nm_new, 1)
    print("[propertyhash_guest] ok (init engine nameMap)")
else:
    print("[propertyhash_guest] init(engine) nameMap anchor missing (skipped)")

init_other_nm_old = """    if (eng) {
        new (&nameMap) SharedInternalClassData<PropertyKey>(eng);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(eng);
    } else {"""

init_other_nm_new = """    if (eng) {
#if defined(BFREE_GUEST_FIXED_STACK)
        nameMap.d = nullptr;
        propertyData.d = nullptr;
        bfree_guest_name_map_init(&nameMap, eng);
        bfree_guest_property_data_init(&propertyData, eng);
#else
        new (&nameMap) SharedInternalClassData<PropertyKey>(eng);
        new (&propertyData) SharedInternalClassData<PropertyAttributes>(eng);
#endif
    } else {"""

if init_other_nm_new in text:
    print("[propertyhash_guest] init(other) nameMap already patched")
elif init_other_nm_old in text:
    text = text.replace(init_other_nm_old, init_other_nm_new, 1)
    print("[propertyhash_guest] ok (init other nameMap)")
else:
    print("[propertyhash_guest] init(other) nameMap anchor missing (skipped)")

addmember_nm_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_hash_init(&newClass->propertyTable);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, identifier);"""

addmember_nm_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_property_hash_init(&newClass->propertyTable);
    bfree_guest_name_map_init(&newClass->nameMap, eng);
    bfree_guest_property_data_init(&newClass->propertyData, eng);
#endif
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, identifier);"""

if addmember_nm_new in text:
    print("[propertyhash_guest] addMemberImpl nameMap already patched")
elif addmember_nm_old in text:
    text = text.replace(addmember_nm_old, addmember_nm_new, 1)
    print("[propertyhash_guest] ok (addMemberImpl nameMap)")
else:
    print("[propertyhash_guest] addMemberImpl nameMap anchor missing (skipped)")

grow_old = """void SharedInternalClassDataPrivate<PropertyKey>::grow()
{
    const uint a = alloc() * 2;"""

grow_new = """void SharedInternalClassDataPrivate<PropertyKey>::grow()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    const uint a = alloc() ? alloc() * 2 : 4;
#else
    const uint a = alloc() * 2;
#endif"""

if grow_new in text:
    print("[propertyhash_guest] grow already patched")
elif grow_old in text:
    text = text.replace(grow_old, grow_new, 1)
    print("[propertyhash_guest] ok (nameMap grow)")
else:
    print("[propertyhash_guest] grow anchor missing (skipped)")

skip_old = """    newClass->propertyTable.addEntry(e, newClass->size);
    if (newClass->nameMap.d && newClass->propertyData.d) {
        newClass->nameMap.add(newClass->size, identifier);
        newClass->propertyData.add(newClass->size, data);
    }
    ++newClass->size;"""

skip_new = """    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, identifier);
    newClass->propertyData.add(newClass->size, data);
    ++newClass->size;"""

if skip_new in text:
    print("[propertyhash_guest] addMember skip-null already removed")
elif skip_old in text:
    text = text.replace(skip_old, skip_new, 1)
    print("[propertyhash_guest] ok (addMember skip-null removed)")
else:
    print("[propertyhash_guest] addMember skip-null anchor missing (may be ok)")

pool_upgrade_old = """static SharedInternalClassDataPrivate<PropertyKey> *bfree_guest_name_map_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyKey>));
    if (!mem)
        return nullptr;
    return new (mem) SharedInternalClassDataPrivate<PropertyKey>(eng);
}"""

pool_upgrade_marker = "g_guestNameMapPool"
if pool_upgrade_marker in text:
    print("[propertyhash_guest] BSS pool already present")
elif pool_upgrade_old in text:
    # replace malloc-only helpers with pool version (live WSL drift)
    text = text.replace(
        pool_upgrade_old,
        """static struct {
    SharedInternalClassDataPrivate<PropertyKey> buf[4096];
    unsigned used;
} g_guestNameMapPool;

static SharedInternalClassDataPrivate<PropertyKey> *bfree_guest_name_map_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyKey>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyKey>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyKey>(eng);
    if (g_guestNameMapPool.used < 4096)
        return new (&g_guestNameMapPool.buf[g_guestNameMapPool.used++])
                SharedInternalClassDataPrivate<PropertyKey>(eng);
    return new (&g_guestNameMapPool.buf[4095])
            SharedInternalClassDataPrivate<PropertyKey>(eng);
}""",
        1,
    )
    text = text.replace(
        """static SharedInternalClassDataPrivate<PropertyAttributes> *bfree_guest_property_data_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>));
    if (!mem)
        return nullptr;
    return new (mem) SharedInternalClassDataPrivate<PropertyAttributes>(eng);
}""",
        """static struct {
    SharedInternalClassDataPrivate<PropertyAttributes> buf[4096];
    unsigned used;
} g_guestPropertyDataPool;

static SharedInternalClassDataPrivate<PropertyAttributes> *bfree_guest_property_data_private(ExecutionEngine *eng)
{
    void *mem = std::malloc(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>));
    if (!mem)
        mem = ::operator new(sizeof(SharedInternalClassDataPrivate<PropertyAttributes>), std::nothrow);
    if (mem)
        return new (mem) SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    if (g_guestPropertyDataPool.used < 4096)
        return new (&g_guestPropertyDataPool.buf[g_guestPropertyDataPool.used++])
                SharedInternalClassDataPrivate<PropertyAttributes>(eng);
    return new (&g_guestPropertyDataPool.buf[4095])
            SharedInternalClassDataPrivate<PropertyAttributes>(eng);
}""",
        1,
    )
    text = text.replace(
        """static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
}""",
        """static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
}""",
        1,
    )
    print("[propertyhash_guest] ok (BSS pool upgrade from malloc-only)")
else:
    print("[propertyhash_guest] pool upgrade anchor missing (helper block may differ)")

path.write_text(text)
print("[propertyhash_guest] done")
PY
