#!/usr/bin/env bash
# Guest allocManaged(size, ic): return nullptr when allocData fails (CR2=0x8/0x10).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
text = path.read_text()

old = """    inline typename ManagedType::Data *allocManaged(std::size_t size, Heap::InternalClass *ic)
    {
        Q_STATIC_ASSERT(std::is_trivial_v<typename ManagedType::Data>);
        size = align(size);
        typename ManagedType::Data *d = static_cast<typename ManagedType::Data *>(allocData(size));
        d->internalClass.set(engine, ic);
        Q_ASSERT(d->internalClass && d->internalClass->vtable);
        Q_ASSERT(ic->vtable == ManagedType::staticVTable());
        return d;
    }"""

new = """    inline typename ManagedType::Data *allocManaged(std::size_t size, Heap::InternalClass *ic)
    {
        Q_STATIC_ASSERT(std::is_trivial_v<typename ManagedType::Data>);
        size = align(size);
        typename ManagedType::Data *d = static_cast<typename ManagedType::Data *>(allocData(size));
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!d || !ic)
            return nullptr;
#endif
        d->internalClass.set(engine, ic);
        Q_ASSERT(d->internalClass && d->internalClass->vtable);
        Q_ASSERT(ic->vtable == ManagedType::staticVTable());
        return d;
    }"""

if new in text:
    print("[allocmanaged_ic_null] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[allocmanaged_ic_null] ok")
else:
    raise SystemExit("[allocmanaged_ic_null] anchor missing")
PY
