#!/usr/bin/env bash
# Guest nameMap.add bypass: use grow/setSize/set without SharedInternalClassData::add.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    if (!map->d)
        bfree_guest_name_map_init(map, eng);
    if (!map->d)
        return;
    auto *pd = map->d;
    if (pos == pd->size()) {
        if (pos >= pd->alloc())
            pd->grow();
        pd->setSize(pd->size() + 1);
        pd->set(pos, id);
    }
}
#endif
"""

old_helper = """static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)
{
    if (!map || !eng)
        return;
    if (!map->d)
        bfree_guest_name_map_init(map, eng);
    if (!map->d)
        return;
    auto *pd = map->d;
    if (!pd->data) {
        Heap::MemberData *m = bfree_guest_property_key_grow_pool(8, pos);
        pd->data.set(eng, m);
        pd->setSize(pos);
    }
    if (pos == pd->size()) {
        if (pos >= pd->alloc())
            pd->grow();
        pd->setSize(pd->size() + 1);
        pd->set(pos, id);
    }
}"""

if old_helper in t:
    t = t.replace(old_helper, helper.strip().split("#endif")[0].replace("#if defined(BFREE_GUEST_FIXED_STACK)\n", "").rstrip() + "\n}\n#endif\n", 1)
    print("[namemap_add_guest] ok (helper fix)")
elif "bfree_guest_name_map_add" in t and old_helper not in t:
    print("[namemap_add_guest] helper already fixed")
elif "bfree_guest_name_map_add" not in t:
    anchor = "void SharedInternalClassDataPrivate<PropertyKey>::grow()"
    if anchor not in t:
        raise SystemExit("[namemap_add_guest] grow anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[namemap_add_guest] ok (helper insert)")

add_old = """    bfree_guest_qv4_heartbeat("add_impl_post_entry");
    newClass->nameMap.add(newClass->size, identifier);
    bfree_guest_qv4_heartbeat("add_impl_post_namemap");"""

add_new = """    bfree_guest_qv4_heartbeat("add_impl_post_entry");
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_name_map_add(&newClass->nameMap, newClass->size, identifier, eng);
#else
    newClass->nameMap.add(newClass->size, identifier);
#endif
    bfree_guest_qv4_heartbeat("add_impl_post_namemap");"""

if add_new in t:
    print("[namemap_add_guest] addMemberImpl already patched")
elif add_old in t:
    t = t.replace(add_old, add_new, 1)
    print("[namemap_add_guest] ok (addMemberImpl)")
else:
    print("[namemap_add_guest] addMemberImpl already patched")

p.write_text(t)
print("[namemap_add_guest] done")
PY
