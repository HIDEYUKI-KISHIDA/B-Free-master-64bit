#!/usr/bin/env bash
# Repair qv4internalclass.cpp after accidental helper deletion.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

helpers = """
static Heap::MemberData *bfree_guest_property_key_grow_pool(uint allocSlots, uint sizeSlots)
{
    const unsigned slot = g_guestPropertyKeyMdSlot < 512 ? g_guestPropertyKeyMdSlot++ : 511;
    auto *m = reinterpret_cast<Heap::MemberData *>(g_guestPropertyKeyMdPool[slot]);
    std::memset(m, 0, sizeof(g_guestPropertyKeyMdPool[0]));
    m->init();
    m->values.alloc = allocSlots ? allocSlots : 4;
    m->values.size = sizeSlots;
    return m;
}

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

static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    bfree_guest_qv4_heartbeat("namemap_enter");
    if (!map->d)
        bfree_guest_name_map_init(map, eng);
    if (!map->d)
        return;
    auto *pd = map->d;
    bfree_guest_qv4_heartbeat("namemap_has_d");
    pd->guestForceEmptyData();
    bfree_guest_qv4_heartbeat("namemap_sanitized");
    if (pos == pd->size()) {
        if (pos >= pd->alloc()) {
            bfree_guest_qv4_heartbeat("namemap_pre_grow");
            pd->grow();
            bfree_guest_qv4_heartbeat("namemap_post_grow");
        }
        bfree_guest_qv4_heartbeat("namemap_pre_setsize");
        pd->setSize(pd->size() + 1);
        bfree_guest_qv4_heartbeat("namemap_pre_set");
        pd->set(pos, id);
        bfree_guest_qv4_heartbeat("namemap_post_set");
    }
}
"""

if "static Heap::MemberData *bfree_guest_property_key_grow_pool" not in t:
    anchor = "bool SharedInternalClassDataPrivate<PropertyKey>::guestDataInPool() const"
    if anchor not in t:
        raise SystemExit("[repair] anchor missing")
    t = t.replace(anchor, helpers + anchor, 1)
    p.write_text(t)
    print("[repair] ok")
else:
    print("[repair] already ok")
PY
