#!/usr/bin/env bash
# Guest: allocData BSS fallback; allocObject null guard before init().
set -eu
python3 - <<'PY'
from pathlib import Path

hp = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
t = hp.read_text()

# Revert guest-only IC set: markCustom is no-op; use normal set
ic_old = """        if (!o)
            return nullptr;
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_object_set_ic(o, ic);
#else
        o->internalClass.set(engine, ic);
#endif"""
ic_new = """        if (!o)
            return nullptr;
        o->internalClass.set(engine, ic);"""
if ic_old in t:
    t = t.replace(ic_old, ic_new, 1)
    print("[alloc_guest] ok (revert ic set)")

decl_old = """
#if defined(BFREE_GUEST_FIXED_STACK)
inline void bfree_guest_object_set_ic(Heap::Object *o, Heap::InternalClass *ic)
{
    if (!o)
        return;
    *reinterpret_cast<Heap::InternalClass **>(&o->internalClass) = ic;
}
#endif

class Q_QML_EXPORT MemoryManager"""
if decl_old in t:
    t = t.replace(decl_old, "\nclass Q_QML_EXPORT MemoryManager", 1)
    print("[alloc_guest] ok (remove ic helper decl)")

alloc_obj_old = """    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocObject(Heap::InternalClass *ic, Args&&... args)
    {
        typename ObjectType::Data *d = allocateObject<ObjectType>(ic);
        d->init(std::forward<Args>(args)...);
        return d;
    }

    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocObject(InternalClass *ic, Args&&... args)
    {
        typename ObjectType::Data *d = allocateObject<ObjectType>(ic);
        d->init(std::forward<Args>(args)...);
        return d;
    }"""

alloc_obj_new = """    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocObject(Heap::InternalClass *ic, Args&&... args)
    {
        typename ObjectType::Data *d = allocateObject<ObjectType>(ic);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!d)
            return nullptr;
#endif
        d->init(std::forward<Args>(args)...);
        return d;
    }

    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocObject(InternalClass *ic, Args&&... args)
    {
        typename ObjectType::Data *d = allocateObject<ObjectType>(ic);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!d)
            return nullptr;
#endif
        d->init(std::forward<Args>(args)...);
        return d;
    }"""

if alloc_obj_new in t:
    print("[alloc_guest] allocObject already patched")
elif alloc_obj_old in t:
    t = t.replace(alloc_obj_old, alloc_obj_new, 1)
    print("[alloc_guest] ok (allocObject null guard)")
else:
    raise SystemExit("[alloc_guest] allocObject anchor missing")

hp.write_text(t)

cp = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm.cpp")
ct = cp.read_text()

alloc_data_old = """    HeapItem *m = allocate(&blockAllocator, size);
    memset(m, 0, size);
    return *m;
}"""

alloc_data_new = """    HeapItem *m = allocate(&blockAllocator, size);
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m)
        return bfree_guest_alloc_data_fallback(size);
#endif
    memset(m, 0, size);
    return *m;
}"""

if alloc_data_new in ct:
    print("[alloc_guest] allocData already patched")
elif alloc_data_old in ct:
    if "bfree_guest_alloc_data_fallback" not in ct:
        raise SystemExit("[alloc_guest] pool helper missing in mm.cpp")
    ct = ct.replace(alloc_data_old, alloc_data_new, 1)
    print("[alloc_guest] ok (allocData fallback)")
else:
    raise SystemExit("[alloc_guest] allocData anchor missing")

member_set_old = """        o->memberData.set(engine, m);
        if (Heap::InternalClass *mdIc = engine->internalClasses(EngineBase::Class_MemberData))
            m->internalClass.set(engine, mdIc);"""
member_set_new = """        bfree_guest_object_set_memberdata(o, m);
        if (Heap::InternalClass *mdIc = engine->internalClasses(EngineBase::Class_MemberData))
            *reinterpret_cast<Heap::InternalClass **>(&m->internalClass) = mdIc;"""

if member_set_new in ct:
    print("[alloc_guest] member set already patched")
elif member_set_old in ct:
    ct = ct.replace(member_set_old, member_set_new, 1)
    print("[alloc_guest] ok (member set bypass)")

chunk_old = """        Chunk *newChunk = chunkAllocator->allocate();
        Q_V4_PROFILE_ALLOC(engine, Chunk::DataSize, Profiling::HeapPage);
        chunks.push_back(newChunk);
        nextFree = newChunk->first();"""
chunk_new = """        Chunk *newChunk = chunkAllocator->allocate();
        if (!newChunk)
            return nullptr;
        Q_V4_PROFILE_ALLOC(engine, Chunk::DataSize, Profiling::HeapPage);
        chunks.push_back(newChunk);
        nextFree = newChunk->first();"""

if chunk_new in ct:
    print("[alloc_guest] BlockAllocator chunk guard already patched")
elif chunk_old in ct:
    ct = ct.replace(chunk_old, chunk_new, 1)
    print("[alloc_guest] ok (BlockAllocator chunk guard)")
else:
    print("[alloc_guest] BlockAllocator chunk anchor missing (skipped)")

cp.write_text(ct)
print("[alloc_guest] done")
PY
