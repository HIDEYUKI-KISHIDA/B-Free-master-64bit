#!/usr/bin/env bash
# Guest QV4 MemoryManager: avoid Scope/Scoped in allocateObject()/allocate() (null ic->changeVTable).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
text = path.read_text()

alloc_obj_old = """    template <typename ObjectType>
    typename ObjectType::Data *allocateObject()
    {
        Scope scope(engine);
        Scoped<InternalClass> ic(scope,  ObjectType::defaultInternalClass(engine));
        ic = ic->changeVTable(ObjectType::staticVTable());
        ic = ic->changePrototype(ObjectType::defaultPrototype(engine)->d());
        return allocateObject<ObjectType>(ic);
    }"""

alloc_obj_new = """    template <typename ObjectType>
    typename ObjectType::Data *allocateObject()
    {
#if defined(BFREE_GUEST_FIXED_STACK)
        Heap::InternalClass *base = ObjectType::defaultInternalClass(engine);
        if (!base)
            return nullptr;
        Heap::InternalClass *ic = base->changeVTable(ObjectType::staticVTable());
        if (!ic)
            return nullptr;
        QV4::Object *proto = ObjectType::defaultPrototype(engine);
        ic = ic->changePrototype(proto ? proto->d() : nullptr);
        if (!ic)
            return nullptr;
        return allocateObject<ObjectType>(ic);
#else
        Scope scope(engine);
        Scoped<InternalClass> ic(scope,  ObjectType::defaultInternalClass(engine));
        ic = ic->changeVTable(ObjectType::staticVTable());
        ic = ic->changePrototype(ObjectType::defaultPrototype(engine)->d());
        return allocateObject<ObjectType>(ic);
#endif
    }"""

alloc_old = """    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocate(Args&&... args)
    {
        Scope scope(engine);
        Scoped<ObjectType> t(scope, allocateObject<ObjectType>());
        t->d_unchecked()->init(std::forward<Args>(args)...);
        return t->d();
    }"""

alloc_new = """    template <typename ObjectType, typename... Args>
    typename ObjectType::Data *allocate(Args&&... args)
    {
#if defined(BFREE_GUEST_FIXED_STACK)
        typename ObjectType::Data *d = allocateObject<ObjectType>();
        if (d)
            d->init(std::forward<Args>(args)...);
        return d;
#else
        Scope scope(engine);
        Scoped<ObjectType> t(scope, allocateObject<ObjectType>());
        t->d_unchecked()->init(std::forward<Args>(args)...);
        return t->d();
#endif
    }"""

changed = False
if alloc_obj_new in text:
    print("[patch_qv4mm_allocate] allocateObject guest already applied")
elif alloc_obj_old in text:
    text = text.replace(alloc_obj_old, alloc_obj_new, 1)
    changed = True
    print("[patch_qv4mm_allocate] ok (allocateObject guest)")
else:
    raise SystemExit("[patch_qv4mm_allocate] allocateObject anchor missing")

if alloc_new in text:
    print("[patch_qv4mm_allocate] allocate guest already applied")
elif alloc_old in text:
    text = text.replace(alloc_old, alloc_new, 1)
    changed = True
    print("[patch_qv4mm_allocate] ok (allocate guest)")
else:
    raise SystemExit("[patch_qv4mm_allocate] allocate anchor missing")

alloc_managed_old = """    template<typename ManagedType>
    inline typename ManagedType::Data *allocManaged()
    {
        auto constexpr size = sizeof(typename ManagedType::Data);
        Scope scope(engine);
        Scoped<InternalClass> ic(scope, ManagedType::defaultInternalClass(engine));
        return allocManaged<ManagedType>(size, ic);
    }"""

alloc_managed_new = """    template<typename ManagedType>
    inline typename ManagedType::Data *allocManaged()
    {
        auto constexpr size = sizeof(typename ManagedType::Data);
#if defined(BFREE_GUEST_FIXED_STACK)
        Heap::InternalClass *ic = ManagedType::defaultInternalClass(engine);
        if (!ic)
            return nullptr;
        return allocManaged<ManagedType>(size, ic);
#else
        Scope scope(engine);
        Scoped<InternalClass> ic(scope, ManagedType::defaultInternalClass(engine));
        return allocManaged<ManagedType>(size, ic);
#endif
    }"""

alloc_fn_old = """    template <typename ManagedType, typename... Args>
    typename ManagedType::Data *alloc(Args&&... args)
    {
        Scope scope(engine);
        Scoped<ManagedType> t(scope, allocManaged<ManagedType>());
        t->d_unchecked()->init(std::forward<Args>(args)...);
        return t->d();
    }"""

alloc_fn_new = """    template <typename ManagedType, typename... Args>
    typename ManagedType::Data *alloc(Args&&... args)
    {
#if defined(BFREE_GUEST_FIXED_STACK)
        typename ManagedType::Data *d = allocManaged<ManagedType>();
        if (d)
            d->init(std::forward<Args>(args)...);
        return d;
#else
        Scope scope(engine);
        Scoped<ManagedType> t(scope, allocManaged<ManagedType>());
        t->d_unchecked()->init(std::forward<Args>(args)...);
        return t->d();
#endif
    }"""

if alloc_managed_new in text:
    print("[patch_qv4mm_allocate] allocManaged guest already applied")
elif alloc_managed_old in text:
    text = text.replace(alloc_managed_old, alloc_managed_new, 1)
    changed = True
    print("[patch_qv4mm_allocate] ok (allocManaged guest)")
else:
    raise SystemExit("[patch_qv4mm_allocate] allocManaged anchor missing")

if alloc_fn_new in text:
    print("[patch_qv4mm_allocate] alloc guest already applied")
elif alloc_fn_old in text:
    text = text.replace(alloc_fn_old, alloc_fn_new, 1)
    changed = True
    print("[patch_qv4mm_allocate] ok (alloc guest)")
else:
    raise SystemExit("[patch_qv4mm_allocate] alloc anchor missing")

alloc_managed_size_old = """    template<typename ManagedType>
    inline typename ManagedType::Data *allocManaged(std::size_t size)
    {
        Scope scope(engine);
        Scoped<InternalClass> ic(scope, ManagedType::defaultInternalClass(engine));
        return allocManaged<ManagedType>(size, ic);
    }"""

alloc_managed_size_new = """    template<typename ManagedType>
    inline typename ManagedType::Data *allocManaged(std::size_t size)
    {
#if defined(BFREE_GUEST_FIXED_STACK)
        Heap::InternalClass *ic = ManagedType::defaultInternalClass(engine);
        if (!ic)
            return nullptr;
        return allocManaged<ManagedType>(size, ic);
#else
        Scope scope(engine);
        Scoped<InternalClass> ic(scope, ManagedType::defaultInternalClass(engine));
        return allocManaged<ManagedType>(size, ic);
#endif
    }"""

if alloc_managed_size_new in text:
    print("[patch_qv4mm_allocate] allocManaged(size) guest already applied")
elif alloc_managed_size_old in text:
    text = text.replace(alloc_managed_size_old, alloc_managed_size_new, 1)
    changed = True
    print("[patch_qv4mm_allocate] ok (allocManaged size guest)")
else:
    raise SystemExit("[patch_qv4mm_allocate] allocManaged(size) anchor missing")

if changed:
    path.write_text(text)
print("[patch_qv4mm_allocate] done")
PY
