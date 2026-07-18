#!/usr/bin/env bash
# Fix guestForceEmptyData + pool bounds in qv4internalclass.cpp
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

t = t.replace(
    "&g_guestPropertyKeyMdPool[512][0]",
    "&g_guestPropertyKeyMdPool[511][0] + sizeof(g_guestPropertyKeyMdPool[0])",
)

gfe_old = """void SharedInternalClassDataPrivate<PropertyKey>::guestForceEmptyData()
{
    if (!data)
        return;
    if (bfree_guest_memberdata_ptr_plausible(data))
        return;
    bfree_guest_memberdata_ptr_set(data, nullptr);
}"""

gfe_new = """void SharedInternalClassDataPrivate<PropertyKey>::guestForceEmptyData()
{
    Heap::MemberData **slot = reinterpret_cast<Heap::MemberData **>(&data);
    if (!*slot)
        return;
    if (bfree_guest_memberdata_ptr_plausible(*slot))
        return;
    *slot = nullptr;
}"""

if gfe_new in t:
    print("[pool_bounds] guestForceEmptyData already patched")
elif gfe_old in t:
    t = t.replace(gfe_old, gfe_new, 1)
    print("[pool_bounds] ok (guestForceEmptyData)")
else:
    raise SystemExit("[pool_bounds] guestForceEmptyData anchor missing")

pool_old = """    if (g_guestNameMapSlot < 4096)
        return new (g_guestNameMapPool[g_guestNameMapSlot++])
                SharedInternalClassDataPrivate<PropertyKey>(eng);
    return new (g_guestNameMapPool[4095])
            SharedInternalClassDataPrivate<PropertyKey>(eng);"""

pool_new = """    if (g_guestNameMapSlot < 4096) {
        void *mem = g_guestNameMapPool[g_guestNameMapSlot++];
        std::memset(mem, 0, sizeof(g_guestNameMapPool[0]));
        return new (mem) SharedInternalClassDataPrivate<PropertyKey>(eng);
    }
    std::memset(g_guestNameMapPool[4095], 0, sizeof(g_guestNameMapPool[0]));
    return new (g_guestNameMapPool[4095])
            SharedInternalClassDataPrivate<PropertyKey>(eng);"""

if pool_new in t:
    print("[pool_bounds] name map pool already patched")
elif pool_old in t:
    t = t.replace(pool_old, pool_new, 1)
    print("[pool_bounds] ok (name map pool memset)")
else:
    print("[pool_bounds] name map pool anchor missing (skipped)")

p.write_text(t)
print("[pool_bounds] done")
PY
