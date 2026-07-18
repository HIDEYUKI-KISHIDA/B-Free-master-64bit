#!/usr/bin/env bash
# Fine-grained heartbeats inside bfree_guest_name_map_add for crash isolation.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

old = """static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)
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
}"""

new = """static void bfree_guest_name_map_add(SharedInternalClassData<PropertyKey> *map, uint pos, PropertyKey id, ExecutionEngine *eng)
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
}"""

if "namemap_enter" in t:
    print("[namemap_hb] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[namemap_hb] ok")
else:
    raise SystemExit("[namemap_hb] anchor missing")
PY
