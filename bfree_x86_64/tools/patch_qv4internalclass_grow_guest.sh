#!/usr/bin/env bash
# Guest PropertyKey grow: BSS pool when data is null or MemberData::allocate fails.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

grow_marker = "bfree_guest_property_key_grow_pool"
if grow_marker not in t:
    helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
alignas(Heap::MemberData)
static char g_guestPropertyKeyMdPool[512][sizeof(Heap::MemberData) + 8 * sizeof(Value)];
static unsigned g_guestPropertyKeyMdSlot = 0;

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
#endif
"""
    anchor = "void SharedInternalClassDataPrivate<PropertyKey>::grow()"
    if anchor not in t:
        raise SystemExit("[grow_guest] grow anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[grow_guest] ok (pool helper)")

grow_old = """void SharedInternalClassDataPrivate<PropertyKey>::grow()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    const uint a = alloc() ? alloc() * 2 : 4;
#else
    const uint a = alloc() * 2;
#endif
    const uint s = size();
    Heap::MemberData *m = MemberData::allocate(engine, a, data);
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m) {
        alignas(Heap::MemberData) static char guestMemberScratch[sizeof(Heap::MemberData) + 4 * sizeof(Value)];
        m = reinterpret_cast<Heap::MemberData *>(guestMemberScratch);
        std::memset(m, 0, sizeof(guestMemberScratch));
        m->init();
        m->values.alloc = a;
        m->values.size = s;
    }
#endif
    if (m) {
        data.set(engine, m);
        setSize(s);
    }
    Q_ASSERT(alloc() >= a);
}"""

grow_new = """void SharedInternalClassDataPrivate<PropertyKey>::grow()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    const uint a = alloc() ? alloc() * 2 : 4;
#else
    const uint a = alloc() * 2;
#endif
    const uint s = size();
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!data) {
        Heap::MemberData *m = bfree_guest_property_key_grow_pool(a, s);
        data.set(engine, m);
        setSize(s);
        return;
    }
#endif
    Heap::MemberData *m = MemberData::allocate(engine, a, data);
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m)
        m = bfree_guest_property_key_grow_pool(a, s);
#endif
    if (m) {
        data.set(engine, m);
        setSize(s);
    }
    Q_ASSERT(alloc() >= a);
}"""

if grow_new in t:
    print("[grow_guest] grow already patched")
elif grow_old in t:
    t = t.replace(grow_old, grow_new, 1)
    print("[grow_guest] ok (grow)")
else:
    raise SystemExit("[grow_guest] grow body anchor missing")

p.write_text(t)
print("[grow_guest] done")
PY
