#!/usr/bin/env bash
# Guest: sanitize PropertyKey nameMap data pointer; safe size/alloc when data is corrupt.
set -eu
python3 - <<'PY'
from pathlib import Path

hp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass_p.h")
ht = hp.read_text()
decl_old = """    void mark(MarkStack *s);

    int refcount = 1;
private:
    ExecutionEngine *engine;
    WriteBarrier::Pointer<Heap::MemberData> data;
};"""
decl_new = """    void mark(MarkStack *s);

#if defined(BFREE_GUEST_FIXED_STACK)
    void guestForceEmptyData();
    bool guestDataInPool() const;
#endif

    int refcount = 1;
private:
    ExecutionEngine *engine;
    WriteBarrier::Pointer<Heap::MemberData> data;
};"""
if "guestForceEmptyData" in ht:
    print("[namemap_sanitize] header already patched")
elif decl_old in ht:
    hp.write_text(ht.replace(decl_old, decl_new, 1))
    print("[namemap_sanitize] ok (header)")
else:
    raise SystemExit("[namemap_sanitize] header anchor missing")

cp = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = cp.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static bool bfree_guest_memberdata_ptr_plausible(const Heap::MemberData *m)
{
    if (!m)
        return false;
    const char *c = reinterpret_cast<const char *>(m);
    const char *a = reinterpret_cast<const char *>(&g_guestPropertyKeyMdPool[0][0]);
    const char *b = reinterpret_cast<const char *>(&g_guestPropertyKeyMdPool[511][0]) + sizeof(g_guestPropertyKeyMdPool[0]);
    if (c >= a && c < b)
        return true;
    const uintptr_t p = reinterpret_cast<uintptr_t>(m);
    return p >= 0x1000000ULL && p < 0x80000000ULL && (p & 7) == 0;
}

bool SharedInternalClassDataPrivate<PropertyKey>::guestDataInPool() const
{
    const Heap::MemberData *m = data;
    if (!m)
        return false;
    const char *c = reinterpret_cast<const char *>(m);
    const char *a = reinterpret_cast<const char *>(&g_guestPropertyKeyMdPool[0][0]);
    const char *b = reinterpret_cast<const char *>(&g_guestPropertyKeyMdPool[511][0]) + sizeof(g_guestPropertyKeyMdPool[0]);
    return c >= a && c < b;
}

void SharedInternalClassDataPrivate<PropertyKey>::guestForceEmptyData()
{
    if (!data)
        return;
    if (bfree_guest_memberdata_ptr_plausible(data))
        return;
    bfree_guest_memberdata_ptr_set(data, nullptr);
}
#endif
"""

if "guestForceEmptyData" not in t or "guestDataInPool" not in t:
    anchor = "void SharedInternalClassDataPrivate<PropertyKey>::grow()"
    if anchor not in t:
        raise SystemExit("[namemap_sanitize] grow anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[namemap_sanitize] ok (methods)")

alloc_old = """uint SharedInternalClassDataPrivate<PropertyKey>::alloc() const
{
    return data ? data->values.alloc : 0;
}"""
alloc_new = """uint SharedInternalClassDataPrivate<PropertyKey>::alloc() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!bfree_guest_memberdata_ptr_plausible(data))
        return 0;
#endif
    return data ? data->values.alloc : 0;
}"""

size_old = """uint SharedInternalClassDataPrivate<PropertyKey>::size() const
{
    return data ? data->values.size : 0;
}"""
size_new = """uint SharedInternalClassDataPrivate<PropertyKey>::size() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!bfree_guest_memberdata_ptr_plausible(data))
        return 0;
#endif
    return data ? data->values.size : 0;
}"""

if alloc_new in t:
    print("[namemap_sanitize] alloc already patched")
elif alloc_old in t:
    t = t.replace(alloc_old, alloc_new, 1)
    print("[namemap_sanitize] ok (alloc)")
else:
    raise SystemExit("[namemap_sanitize] alloc anchor missing")

if size_new in t:
    print("[namemap_sanitize] size already patched")
elif size_old in t:
    t = t.replace(size_old, size_new, 1)
    print("[namemap_sanitize] ok (size)")
else:
    raise SystemExit("[namemap_sanitize] size anchor missing")

init_old = """static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
}"""
init_new = """static void bfree_guest_name_map_init(SharedInternalClassData<PropertyKey> *map, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    map->d = bfree_guest_name_map_private(eng);
    if (map->d)
        map->d->guestForceEmptyData();
}"""

if init_new in t:
    print("[namemap_sanitize] init already patched")
elif init_old in t:
    t = t.replace(init_old, init_new, 1)
    print("[namemap_sanitize] ok (init)")
else:
    raise SystemExit("[namemap_sanitize] init anchor missing")

add_old = """    bfree_guest_qv4_heartbeat("namemap_has_d");
    if (pos == pd->size()) {"""
add_new = """    bfree_guest_qv4_heartbeat("namemap_has_d");
    pd->guestForceEmptyData();
    bfree_guest_qv4_heartbeat("namemap_sanitized");
    if (pos == pd->size()) {"""

if "namemap_sanitized" in t:
    print("[namemap_sanitize] namemap_add already patched")
elif add_old in t:
    t = t.replace(add_old, add_new, 1)
    print("[namemap_sanitize] ok (namemap_add)")
else:
    raise SystemExit("[namemap_sanitize] namemap_add anchor missing")

cp.write_text(t)
print("[namemap_sanitize] done")
PY
