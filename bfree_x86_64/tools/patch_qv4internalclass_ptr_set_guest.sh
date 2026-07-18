#!/usr/bin/env bash
# Guest: bypass WriteBarrier::Pointer::set for BSS pool MemberData in PropertyKey grow/set.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static void bfree_guest_memberdata_ptr_set(WriteBarrier::Pointer<Heap::MemberData> &p, Heap::MemberData *m)
{
    *reinterpret_cast<Heap::MemberData **>(&p) = m;
}
#endif
"""

if "bfree_guest_memberdata_ptr_set" not in t:
    anchor = "static Heap::MemberData *bfree_guest_property_key_grow_pool"
    if anchor not in t:
        raise SystemExit("[ptr_set_guest] pool anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[ptr_set_guest] ok (helper)")

grow_old = """    if (!data) {
        Heap::MemberData *m = bfree_guest_property_key_grow_pool(a, s);
        data.set(engine, m);
        setSize(s);
        return;
    }"""
grow_new = """    if (!data) {
        Heap::MemberData *m = bfree_guest_property_key_grow_pool(a, s);
        bfree_guest_memberdata_ptr_set(data, m);
        setSize(s);
        return;
    }"""

grow_old2 = """    if (m) {
        data.set(engine, m);
        setSize(s);
    }
    Q_ASSERT(alloc() >= a);
}"""

grow_new2 = """    if (m) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_memberdata_ptr_set(data, m);
#else
        data.set(engine, m);
#endif
        setSize(s);
    }
    Q_ASSERT(alloc() >= a);
}"""

set_old = """void SharedInternalClassDataPrivate<PropertyKey>::set(uint i, PropertyKey t)
{
    Q_ASSERT(data && i < size());
    QV4::WriteBarrier::markCustom(engine, [&](QV4::MarkStack *stack) {
        if constexpr (QV4::WriteBarrier::isInsertionBarrier)
            if (auto string = t.asStringOrSymbol())
                string->mark(stack);
    });
    data->values.values[i].rawValueRef() = t.id();
}"""

set_new = """void SharedInternalClassDataPrivate<PropertyKey>::set(uint i, PropertyKey t)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (data && i < size()) {
        data->values.values[i].rawValueRef() = t.id();
        return;
    }
#endif
    Q_ASSERT(data && i < size());
    QV4::WriteBarrier::markCustom(engine, [&](QV4::MarkStack *stack) {
        if constexpr (QV4::WriteBarrier::isInsertionBarrier)
            if (auto string = t.asStringOrSymbol())
                string->mark(stack);
    });
    data->values.values[i].rawValueRef() = t.id();
}"""

if grow_new in t:
    print("[ptr_set_guest] grow null-path already patched")
elif grow_old in t:
    t = t.replace(grow_old, grow_new, 1)
    print("[ptr_set_guest] ok (grow null-path)")
else:
    raise SystemExit("[ptr_set_guest] grow null-path anchor missing")

if grow_new2 in t:
    print("[ptr_set_guest] grow assign-path already patched")
elif grow_old2 in t:
    t = t.replace(grow_old2, grow_new2, 1)
    print("[ptr_set_guest] ok (grow assign-path)")
else:
    raise SystemExit("[ptr_set_guest] grow assign-path anchor missing")

if set_new in t:
    print("[ptr_set_guest] set already patched")
elif set_old in t:
    t = t.replace(set_old, set_new, 1)
    print("[ptr_set_guest] ok (set)")
else:
    raise SystemExit("[ptr_set_guest] set anchor missing")

p.write_text(t)
print("[ptr_set_guest] done")
PY
