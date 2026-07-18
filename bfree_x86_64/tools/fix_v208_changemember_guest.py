from pathlib import Path

ic = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = ic.read_text()

anchor = """Heap::InternalClass *InternalClass::changeMember(
        PropertyKey identifier, PropertyAttributes data, InternalClassEntry *entry)
{
    if (!data.isEmpty())
        data.resolve();
    PropertyHash::Entry *e = findEntry(identifier);"""

guest = """Heap::InternalClass *InternalClass::changeMember(
        PropertyKey identifier, PropertyAttributes data, InternalClassEntry *entry)
{
    if (!data.isEmpty())
        data.resolve();
#if defined(BFREE_GUEST_FIXED_STACK)
    ExecutionEngine *eng = engine;
    if (!eng)
        eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!propertyData.d && eng)
        bfree_guest_property_data_init(&propertyData, eng);
    PropertyHash::Entry *e = findEntry(identifier);
    if (!e || e->index >= size || !propertyData.d || !eng)
        return this;
    if (entry) {
        entry->index = e->index;
        entry->setterIndex = e->setterIndex;
        entry->attributes = data;
    }
    bfree_guest_property_data_add(&propertyData, e->index, data, eng);
    ++numRedundantTransitions;
    return this;
#else
    PropertyHash::Entry *e = findEntry(identifier);"""

guest_tail = """    return cleanInternalClass(newClass);
}
"""

if "bfree_guest_property_data_add(&propertyData, e->index, data, eng)" in t:
    print("[v208] changeMember guest stub already patched")
elif anchor not in t:
    raise SystemExit("[v208] changeMember anchor missing")
else:
    if "#else\n    PropertyHash::Entry *e = findEntry(identifier);" in t:
        raise SystemExit("[v208] changeMember partially patched; fix source manually")
    t = t.replace(anchor, guest, 1)
    close_anchor = "    return cleanInternalClass(newClass);\n}"
    if close_anchor not in t:
        raise SystemExit("[v208] changeMember close anchor missing")
    t = t.replace(close_anchor, "    return cleanInternalClass(newClass);\n#endif\n}", 1)
    print("[v208] changeMember guest in-place stub")

ic.write_text(t)
print("[v208] done")
