#!/usr/bin/env bash
# Guest MemberData::allocate: BSS pool when allocManaged returns null.
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4memberdata.cpp")
text = path.read_text()

helper = """#if defined(BFREE_GUEST_FIXED_STACK)
alignas(QV4::Heap::MemberData)
static char g_guestMemberDataPool[512][sizeof(QV4::Heap::MemberData) + 8 * sizeof(QV4::Value)];
static unsigned g_guestMemberDataSlot = 0;

static QV4::Heap::MemberData *bfree_guest_member_data_fallback(uint allocSlots)
{
    const unsigned slot = g_guestMemberDataSlot < 512 ? g_guestMemberDataSlot++ : 511;
    auto *m = reinterpret_cast<QV4::Heap::MemberData *>(g_guestMemberDataPool[slot]);
    std::memset(m, 0, sizeof(g_guestMemberDataPool[0]));
    m->init();
    m->values.alloc = allocSlots;
    m->values.size = allocSlots;
    return m;
}
#endif

"""

marker = "bfree_guest_member_data_fallback"
if marker not in text:
    anchor = "Heap::MemberData *MemberData::allocate(ExecutionEngine *e, uint n, Heap::MemberData *old)"
    if anchor not in text:
        raise SystemExit("[memberdata_alloc] anchor missing")
    if "#include <cstring>" not in text:
        text = text.replace('#include "qv4value_p.h"\n', '#include "qv4value_p.h"\n#include <cstring>\n', 1)
    text = text.replace(anchor, helper + anchor, 1)
    print("[memberdata_alloc] ok (helper)")
else:
    print("[memberdata_alloc] helper already present")

old_tail = """    } else {
        m = e->memoryManager->allocManaged<MemberData>(alloc);
        m->init();
    }

    m->values.alloc = static_cast<uint>((alloc - sizeof(Heap::MemberData) + sizeof(Value))/sizeof(Value));
    m->values.size = m->values.alloc;
    return m;
}"""

new_tail = """    } else {
        m = e->memoryManager->allocManaged<MemberData>(alloc);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!m) {
            const uint nSlots = static_cast<uint>((alloc - sizeof(Heap::MemberData) + sizeof(Value)) / sizeof(Value));
            m = bfree_guest_member_data_fallback(nSlots ? nSlots : 4);
        } else
#endif
            m->init();
    }

    m->values.alloc = static_cast<uint>((alloc - sizeof(Heap::MemberData) + sizeof(Value))/sizeof(Value));
    m->values.size = m->values.alloc;
    return m;
}"""

if new_tail in text:
    print("[memberdata_alloc] already applied")
elif old_tail in text:
    text = text.replace(old_tail, new_tail, 1)
    print("[memberdata_alloc] ok")
else:
    raise SystemExit("[memberdata_alloc] tail anchor missing")

path.write_text(text)
print("[memberdata_alloc] done")
PY
