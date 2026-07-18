#!/usr/bin/env python3
"""v260b: guest resolved-type slot uses ints only (no static TypeReference ctor)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old_slot = """namespace {
struct GuestResolvedTypeSlot {
    int key = -1;
    QQmlTypeData::TypeReference ref;
    bool valid = false;
} g_guestResolvedTypeSlot;
}"""

new_slot = """namespace {
int g_guestResolvedKey = -1;
int g_guestResolvedTypeIndex = -1;
QTypeRevision g_guestResolvedVersion;
bool g_guestResolvedNeedsCreation = false;
bool g_guestResolvedValid = false;
}"""

if old_slot in tt:
    tt = tt.replace(old_slot, new_slot, 1)
elif "g_guestResolvedTypeIndex" in tt:
    print("[v260b] int slot already patched")
    raise SystemExit(0)
else:
    raise SystemExit("[v260b] guest slot anchor missing")

old_store = """        bfree_guest_qv4_heartbeat("typedata_resolve_store");
        g_guestResolvedTypeSlot.key = unresolvedRef.key();
        g_guestResolvedTypeSlot.ref = ref;
        g_guestResolvedTypeSlot.valid = true;
        bfree_guest_qv4_heartbeat("typedata_resolve_store_ok");"""

new_store = """        bfree_guest_qv4_heartbeat("typedata_resolve_store");
        g_guestResolvedKey = unresolvedRef.key();
        g_guestResolvedTypeIndex = ref.type.isValid() ? ref.type.index() : -1;
        g_guestResolvedVersion = ref.version;
        g_guestResolvedNeedsCreation = unresolvedRef->needsCreation;
        g_guestResolvedValid = g_guestResolvedTypeIndex >= 0;
        bfree_guest_qv4_heartbeat("typedata_resolve_store_ok");"""

if old_store not in tt:
    raise SystemExit("[v260b] store anchor missing")
tt = tt.replace(old_store, new_store, 1)

old_cache = """    if (g_guestResolvedTypeSlot.valid) {
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
    }"""

new_cache = """    if (g_guestResolvedValid && g_guestResolvedTypeIndex >= 0) {
        auto ref = std::make_unique<QV4::ResolvedTypeReference>();
        QQmlType qmlType = QQmlMetaType::qmlTypeById(g_guestResolvedTypeIndex);
        if (qmlType.isValid()) {
            ref->setType(qmlType);
            if (qmlType.containsRevisionedAttributes()) {
                Q_ASSERT(qmlType.metaObject());
                ref->setTypePropertyCache(
                        QQmlMetaType::propertyCache(qmlType, g_guestResolvedVersion));
            }
        }
        ref->setVersion(g_guestResolvedVersion);
        ref->doDynamicTypeCheck();
        resolvedTypeCache->insert(g_guestResolvedKey, ref.release());
        bfree_guest_qv4_heartbeat("typedata_build_cache_ok");
        QQmlError noError;
        return noError;
    }"""

if old_cache not in tt:
    raise SystemExit("[v260b] cache anchor missing")
tt = tt.replace(old_cache, new_cache, 1)

td.write_text(tt)
print("[v260b] guest resolved type int slot")
