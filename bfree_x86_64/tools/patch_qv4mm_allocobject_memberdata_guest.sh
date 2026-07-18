#!/usr/bin/env bash
# Guest allocObjectWithMemberData: BSS pool when allocData fails; null guards.
set -eu
python3 - <<'PY'
from pathlib import Path

hp = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
ht = hp.read_text()

alloc_obj_ic_old = """    template <typename ObjectType>
    typename ObjectType::Data *allocateObject(Heap::InternalClass *ic)
    {
        Heap::Object *o = allocObjectWithMemberData(ObjectType::staticVTable(), ic->size);
        o->internalClass.set(engine, ic);
        Q_ASSERT(o->internalClass.get() && o->vtable());
        Q_ASSERT(o->vtable() == ObjectType::staticVTable());
        return static_cast<typename ObjectType::Data *>(o);
    }"""

alloc_obj_ic_new = """    template <typename ObjectType>
    typename ObjectType::Data *allocateObject(Heap::InternalClass *ic)
    {
        if (!ic)
            return nullptr;
        Heap::Object *o = allocObjectWithMemberData(ObjectType::staticVTable(), ic->size);
        if (!o)
            return nullptr;
        o->internalClass.set(engine, ic);
        Q_ASSERT(o->internalClass.get() && o->vtable());
        Q_ASSERT(o->vtable() == ObjectType::staticVTable());
        return static_cast<typename ObjectType::Data *>(o);
    }"""

if alloc_obj_ic_new not in ht:
    if alloc_obj_ic_old not in ht:
        raise SystemExit("[allocobject_memberdata] allocateObject anchor missing")
    hp.write_text(ht.replace(alloc_obj_ic_old, alloc_obj_ic_new, 1))
    print("[allocobject_memberdata] ok (allocateObject ic guard)")
else:
    print("[allocobject_memberdata] allocateObject(ic) already patched")

cp = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm.cpp")
t = cp.read_text()

pool = """
#if defined(BFREE_GUEST_FIXED_STACK)
#include <cstring>
alignas(QV4::Heap::Object)
static char g_guestObjectScratch[8192];
static unsigned g_guestObjectScratchUsed = 0;

static QV4::Heap::Base *bfree_guest_alloc_data_fallback(std::size_t size)
{
    const std::size_t aligned = (size + 15) & ~std::size_t(15);
    if (g_guestObjectScratchUsed + aligned <= sizeof(g_guestObjectScratch)) {
        void *p = g_guestObjectScratch + g_guestObjectScratchUsed;
        g_guestObjectScratchUsed += static_cast<unsigned>(aligned);
        std::memset(p, 0, aligned);
        return static_cast<QV4::Heap::Base *>(p);
    }
    static char g_guestObjectBig[8192];
    std::memset(g_guestObjectBig, 0, sizeof(g_guestObjectBig));
    return reinterpret_cast<QV4::Heap::Base *>(g_guestObjectBig);
}

alignas(QV4::Heap::MemberData)
static char g_guestMmMemberPool[256][sizeof(QV4::Heap::MemberData) + 8 * sizeof(QV4::Value)];
static unsigned g_guestMmMemberSlot = 0;

static QV4::Heap::MemberData *bfree_guest_mm_member_fallback(std::size_t memberSize, uint nSlots)
{
    const unsigned slot = g_guestMmMemberSlot < 256 ? g_guestMmMemberSlot++ : 255;
    auto *m = reinterpret_cast<QV4::Heap::MemberData *>(g_guestMmMemberPool[slot]);
    std::memset(m, 0, sizeof(g_guestMmMemberPool[0]));
    m->init();
    if (!nSlots)
        nSlots = static_cast<uint>((memberSize - sizeof(QV4::Heap::MemberData) + sizeof(QV4::Value)) / sizeof(QV4::Value));
    if (!nSlots)
        nSlots = 4;
    m->values.alloc = nSlots;
    m->values.size = nSlots;
    return m;
}
#endif
"""

if "bfree_guest_alloc_data_fallback" not in t:
    anchor = "Heap::Base *MemoryManager::allocData(std::size_t size)"
    if anchor not in t:
        raise SystemExit("[allocobject_memberdata] allocData anchor missing")
    t = t.replace(anchor, pool + anchor, 1)
    print("[allocobject_memberdata] ok (pool helper before allocData)")

fn_old = """    if (nMembers <= vtable->nInlineProperties) {
        o = static_cast<Heap::Object *>(allocData(size));
    } else {"""

fn_new = """    if (nMembers <= vtable->nInlineProperties) {
        o = static_cast<Heap::Object *>(allocData(size));
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!o)
            o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
#endif
    } else {"""

if fn_new not in t:
    if fn_old not in t:
        raise SystemExit("[allocobject_memberdata] inline anchor missing")
    t = t.replace(fn_old, fn_new, 1)
    print("[allocobject_memberdata] ok (inline fallback)")

member_old = """        o->memberData.set(engine, m);
        m->internalClass.set(engine, engine->internalClasses(EngineBase::Class_MemberData));
        Q_ASSERT(o->memberData->internalClass);
        m->values.alloc = static_cast<uint>((memberSize - sizeof(Heap::MemberData) + sizeof(Value))/sizeof(Value));
        m->values.size = o->memberData->values.alloc;
        m->init();"""

member_new = """#if defined(BFREE_GUEST_FIXED_STACK)
        if (!o)
            o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
        if (!m)
            m = bfree_guest_mm_member_fallback(memberSize, nMembers);
        if (!o || !m)
            return nullptr;
        m->init();
        m->values.alloc = static_cast<uint>((memberSize - sizeof(Heap::MemberData) + sizeof(Value))/sizeof(Value));
        if (!m->values.alloc)
            m->values.alloc = nMembers ? nMembers : 4;
        m->values.size = m->values.alloc;
        o->memberData.set(engine, m);
        if (Heap::InternalClass *mdIc = engine->internalClasses(EngineBase::Class_MemberData))
            m->internalClass.set(engine, mdIc);
#else
        o->memberData.set(engine, m);
        m->internalClass.set(engine, engine->internalClasses(EngineBase::Class_MemberData));
        Q_ASSERT(o->memberData->internalClass);
        m->values.alloc = static_cast<uint>((memberSize - sizeof(Heap::MemberData) + sizeof(Value))/sizeof(Value));
        m->values.size = o->memberData->values.alloc;
        m->init();
#endif"""

if member_new not in t:
    if member_old not in t:
        raise SystemExit("[allocobject_memberdata] member anchor missing")
    t = t.replace(member_old, member_new, 1)
    print("[allocobject_memberdata] ok (member path)")
else:
    print("[allocobject_memberdata] member path already patched")

# Guest fallback when combined allocData fails
combined_old = """            HeapItem *mh = reinterpret_cast<HeapItem *>(allocData(totalSize));
            Heap::Base *b = *mh;
            o = static_cast<Heap::Object *>(b);"""
combined_new = """            HeapItem *mh = reinterpret_cast<HeapItem *>(allocData(totalSize));
#if defined(BFREE_GUEST_FIXED_STACK)
            if (!mh) {
                o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
                m = bfree_guest_mm_member_fallback(memberSize, nMembers);
            } else
#endif
            {
            Heap::Base *b = *mh;
            o = static_cast<Heap::Object *>(b);"""

if combined_new not in t:
    if combined_old in t:
        t = t.replace(combined_old, combined_new, 1)
        # close extra brace before o->memberData block
        close_old = """            Chunk::clearBit(c->extendsBitmap, index);
        }
        o->memberData.set(engine, m);"""
        close_new = """            Chunk::clearBit(c->extendsBitmap, index);
            }
        }
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!o)
            o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
        if (!m && nMembers > vtable->nInlineProperties)
            m = bfree_guest_mm_member_fallback(memberSize, nMembers);
#endif
        o->memberData.set(engine, m);"""
        if close_old in t:
            t = t.replace(close_old, close_new, 1)
            print("[allocobject_memberdata] ok (combined fallback)")
        else:
            print("[allocobject_memberdata] combined close anchor missing (skipped)")
    else:
        print("[allocobject_memberdata] combined anchor missing (skipped)")

cp.write_text(t)
print("[allocobject_memberdata] done")
PY
