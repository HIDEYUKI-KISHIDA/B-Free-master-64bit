#!/usr/bin/env python3
"""Guest: avoid QMap insert crash — store single resolved type in static slot."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "g_guestResolvedTypeSlot" in tt:
    print("[v260] guest resolved type store already patched")
    raise SystemExit(0)

slot = """
#if defined(BFREE_GUEST_FIXED_STACK)
namespace {
struct GuestResolvedTypeSlot {
    int key = -1;
    QQmlTypeData::TypeReference ref;
    bool valid = false;
} g_guestResolvedTypeSlot;
}
#endif

"""

anchor = "#include <private/qqmlmetatype_p.h>"
if slot.strip() not in tt:
    tt = tt.replace(anchor, anchor + slot, 1)

old_insert = """        ref.version = version;
        ref.location = unresolvedRef->location;
        ref.needsCreation = unresolvedRef->needsCreation;
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif

    // ### this allows enums to work without explicit import or instantiation of the type
    if (!m_implicitImportLoaded)
        loadImplicitImport();
}"""

new_insert = """        ref.version = version;
        ref.location = unresolvedRef->location;
        ref.needsCreation = unresolvedRef->needsCreation;
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_store");
        g_guestResolvedTypeSlot.key = unresolvedRef.key();
        g_guestResolvedTypeSlot.ref = ref;
        g_guestResolvedTypeSlot.valid = true;
        bfree_guest_qv4_heartbeat("typedata_resolve_store_ok");
#else
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
#endif
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif

    // ### this allows enums to work without explicit import or instantiation of the type
#if !defined(BFREE_GUEST_FIXED_STACK)
    if (!m_implicitImportLoaded)
        loadImplicitImport();
#endif
}"""

if old_insert not in tt:
    raise SystemExit("[v260] resolveTypes insert anchor missing")
tt = tt.replace(old_insert, new_insert, 1)

old_cache = """    m_importCache->populateCache(typeNameCache->data());

    for (auto resolvedType = m_resolvedTypes.constBegin(), end = m_resolvedTypes.constEnd(); resolvedType != end; ++resolvedType) {"""

new_cache = """    m_importCache->populateCache(typeNameCache->data());

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_build_cache");
    if (g_guestResolvedTypeSlot.valid) {
        auto ref = std::make_unique<QV4::ResolvedTypeReference>();
        QQmlType qmlType = g_guestResolvedTypeSlot.ref.type;
        if (qmlType.isValid() && !g_guestResolvedTypeSlot.ref.selfReference) {
            ref->setType(qmlType);
            if (qmlType.containsRevisionedAttributes()) {
                Q_ASSERT(qmlType.metaObject());
                ref->setTypePropertyCache(
                        QQmlMetaType::propertyCache(qmlType, g_guestResolvedTypeSlot.ref.version));
            }
        }
        ref->setVersion(g_guestResolvedTypeSlot.ref.version);
        ref->doDynamicTypeCheck();
        resolvedTypeCache->insert(g_guestResolvedTypeSlot.key, ref.release());
        bfree_guest_qv4_heartbeat("typedata_build_cache_ok");
        QQmlError noError;
        return noError;
    }
#endif

    for (auto resolvedType = m_resolvedTypes.constBegin(), end = m_resolvedTypes.constEnd(); resolvedType != end; ++resolvedType) {"""

if old_cache not in tt:
    raise SystemExit("[v260] buildTypeResolutionCaches anchor missing")
tt = tt.replace(old_cache, new_cache, 1)

td.write_text(tt)
print("[v260] guest resolved type static store + buildTypeResolutionCaches bypass")
