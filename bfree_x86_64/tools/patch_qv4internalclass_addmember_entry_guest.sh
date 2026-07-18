#!/usr/bin/env bash
# Guest Heap::InternalClass::addMember — skip findEntry (null this / empty propertyTable).
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()

old = """Heap::InternalClass *InternalClass::addMember(PropertyKey identifier, PropertyAttributes data, InternalClassEntry *entry)
{
    Q_ASSERT(identifier.isStringOrSymbol());
    if (!data.isEmpty())
        data.resolve();

    PropertyHash::Entry *e = findEntry(identifier);
    if (e)
        return changeMember(identifier, data, entry);

    return addMemberImpl(identifier, data, entry);
}"""

new = """Heap::InternalClass *InternalClass::addMember(PropertyKey identifier, PropertyAttributes data, InternalClassEntry *entry)
{
    Q_ASSERT(identifier.isStringOrSymbol());
    if (!data.isEmpty())
        data.resolve();

#if defined(BFREE_GUEST_FIXED_STACK)
    if (!this)
        return nullptr;
    return addMemberImpl(identifier, data, entry);
#else
    PropertyHash::Entry *e = findEntry(identifier);
    if (e)
        return changeMember(identifier, data, entry);

    return addMemberImpl(identifier, data, entry);
#endif
}"""

if new in t:
    print("[addmember_entry] already applied")
elif old in t:
    p.write_text(t.replace(old, new, 1))
    print("[addmember_entry] ok")
else:
    raise SystemExit("[addmember_entry] anchor missing")
PY
