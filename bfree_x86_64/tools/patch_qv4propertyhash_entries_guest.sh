#!/usr/bin/env bash
# Guest PropertyHashData: ensure entries[] exists when malloc fails (addEntry CR2=0x10).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

helper = """#if defined(BFREE_GUEST_FIXED_STACK)
alignas(PropertyHash::Entry)
static char g_guestPropertyHashEntries[1024][8 * sizeof(PropertyHash::Entry)];
static unsigned g_guestPropertyHashEntriesUsed = 0;

static PropertyHash::Entry *bfree_guest_property_hash_entries(unsigned alloc)
{
    const size_t bytes = alloc * sizeof(PropertyHash::Entry);
    void *mem = std::malloc(bytes);
    if (!mem)
        mem = ::operator new(bytes, std::nothrow);
    if (mem)
        return static_cast<PropertyHash::Entry *>(mem);
    if (g_guestPropertyHashEntriesUsed < 1024)
        return reinterpret_cast<PropertyHash::Entry *>(
                g_guestPropertyHashEntries[g_guestPropertyHashEntriesUsed++]);
    return reinterpret_cast<PropertyHash::Entry *>(g_guestPropertyHashEntries[1023]);
}

static bool bfree_guest_property_hash_entries_is_pooled(const PropertyHash::Entry *entries)
{
    const char *p = reinterpret_cast<const char *>(entries);
    const char *base = &g_guestPropertyHashEntries[0][0];
    const char *end = &g_guestPropertyHashEntries[1024][0];
    return p >= base && p < end;
}
#endif
"""

marker = "static bool bfree_guest_property_hash_entries_is_pooled"
if marker not in text:
    anchor = "PropertyHashData::PropertyHashData(int numBits)"
    if anchor not in text:
        raise SystemExit("[propertyhash_entries_guest] ctor anchor missing")
    text = text.replace(anchor, helper + "\n" + anchor, 1)
    print("[propertyhash_entries_guest] ok (helper)")
else:
    print("[propertyhash_entries_guest] helper already present")

ctor_old = """    alloc = qPrimeForNumBits(numBits);
    entries = (PropertyHash::Entry *)malloc(alloc*sizeof(PropertyHash::Entry));
    memset(entries, 0, alloc*sizeof(PropertyHash::Entry));
}"""

ctor_new = """    alloc = qPrimeForNumBits(numBits);
#if defined(BFREE_GUEST_FIXED_STACK)
    entries = bfree_guest_property_hash_entries(alloc);
#else
    entries = (PropertyHash::Entry *)malloc(alloc*sizeof(PropertyHash::Entry));
#endif
    if (entries)
        memset(entries, 0, alloc*sizeof(PropertyHash::Entry));
}"""

if ctor_new in text:
    print("[propertyhash_entries_guest] ctor already patched")
elif ctor_old in text:
    text = text.replace(ctor_old, ctor_new, 1)
    print("[propertyhash_entries_guest] ok (ctor)")
else:
    raise SystemExit("[propertyhash_entries_guest] ctor anchor missing")

dtor_old = """PropertyHashData::~PropertyHashData() {
        free(entries);
    }"""

dtor_new = """PropertyHashData::~PropertyHashData() {
#if defined(BFREE_GUEST_FIXED_STACK)
        if (!bfree_guest_property_hash_entries_is_pooled(entries))
#endif
            free(entries);
    }"""

if dtor_new in text:
    print("[propertyhash_entries_guest] dtor already patched")
elif dtor_old in text:
    text = text.replace(dtor_old, dtor_new, 1)
    print("[propertyhash_entries_guest] ok (dtor)")
else:
    print("[propertyhash_entries_guest] dtor anchor missing (skipped)")

path.write_text(text)
print("[propertyhash_entries_guest] done")
PY
