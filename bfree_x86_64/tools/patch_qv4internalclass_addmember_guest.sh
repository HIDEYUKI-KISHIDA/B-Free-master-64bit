#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

marker = "    ++newClass->numRedundantTransitions;\n    return newClass;\n#endif\n}\n\nvoid InternalClass::removeChildEntry"
if marker in text:
    print("[patch_addmember] already applied")
    raise SystemExit(0)

start = text.find("Heap::InternalClass *InternalClass::addMemberImpl")
end = text.find("\n}\n\nvoid InternalClass::removeChildEntry", start)
if start < 0 or end < 0:
    raise SystemExit("addMemberImpl block not found")

stub = r'''Heap::InternalClass *InternalClass::addMemberImpl(PropertyKey identifier, PropertyAttributes data, InternalClassEntry *entry)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = engine;
    if (!eng)
        return this;
    if (entry) {
        entry->index = size;
        entry->setterIndex = data.isAccessor() ? size + 1 : UINT_MAX;
        entry->attributes = data;
    }
    Heap::InternalClass *newClass = eng->newClass(this);
    if (!newClass)
        return this;
    newClass->engine = eng;
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, identifier);
    newClass->propertyData.add(newClass->size, data);
    ++newClass->size;
    if (data.isAccessor())
        addDummyEntry(newClass, e);
    ++newClass->numRedundantTransitions;
    return newClass;
#else
    Transition temp = { { identifier }, nullptr, int(data.all()) };
    Transition &t = lookupOrInsertTransition(temp);

    if (entry) {
        entry->index = size;
        entry->setterIndex = data.isAccessor() ? size + 1 : UINT_MAX;
        entry->attributes = data;
    }

    if (t.lookup)
        return t.lookup;

    // create a new class and add it to the tree
    Scope scope(engine);
    Scoped<QV4::InternalClass> ic(scope, engine->newClass(this));
    InternalClass *newClass = ic->d();
    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);

    newClass->nameMap.add(newClass->size, identifier);
    newClass->propertyData.add(newClass->size, data);
    ++newClass->size;
    if (data.isAccessor())
        addDummyEntry(newClass, e);

    t.lookup = newClass;
    Q_ASSERT(t.lookup);
    return newClass;
#endif
}'''

text = text[:start] + stub + text[end+2:]
path.write_text(text)
print("[patch_addmember] ok")
PY
