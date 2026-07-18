#!/usr/bin/env python3
"""Guest: buildTypeResolutionCaches early exit (skip populateCache / QUrl::path)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_build_cache_enter" in tt:
    print("[v263] build cache early exit already patched")
    raise SystemExit(0)

old_fn = """QQmlError QQmlTypeData::buildTypeResolutionCaches(
        QQmlRefPointer<QQmlTypeNameCache> *typeNameCache,
        QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache) const
{
    typeNameCache->adopt(new QQmlTypeNameCache(m_importCache));

    for (const QString &ns: m_namespaces)
        (*typeNameCache)->add(ns);

    // Add any Composite Singletons that were used to the import cache
    for (const QQmlTypeData::TypeReference &singleton: m_compositeSingletons)
        (*typeNameCache)->add(singleton.type.qmlTypeName(), singleton.type.sourceUrl(), singleton.prefix);

    m_importCache->populateCache(typeNameCache->data());

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_build_cache");
    if (g_guestResolvedValid && g_guestResolvedTypeIndex >= 0) {
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
    }
#endif

    for (auto resolvedType = m_resolvedTypes.constBegin(), end = m_resolvedTypes.constEnd(); resolvedType != end; ++resolvedType) {"""

new_fn = """QQmlError QQmlTypeData::buildTypeResolutionCaches(
        QQmlRefPointer<QQmlTypeNameCache> *typeNameCache,
        QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache) const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_build_cache_enter");
    typeNameCache->adopt(new QQmlTypeNameCache(m_importCache));
    if (g_guestResolvedValid && g_guestResolvedTypeIndex >= 0) {
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
    }
#endif

    typeNameCache->adopt(new QQmlTypeNameCache(m_importCache));

    for (const QString &ns: m_namespaces)
        (*typeNameCache)->add(ns);

    // Add any Composite Singletons that were used to the import cache
    for (const QQmlTypeData::TypeReference &singleton: m_compositeSingletons)
        (*typeNameCache)->add(singleton.type.qmlTypeName(), singleton.type.sourceUrl(), singleton.prefix);

    m_importCache->populateCache(typeNameCache->data());

    for (auto resolvedType = m_resolvedTypes.constBegin(), end = m_resolvedTypes.constEnd(); resolvedType != end; ++resolvedType) {"""

if old_fn not in tt:
    raise SystemExit("[v263] buildTypeResolutionCaches anchor missing")

td.write_text(tt.replace(old_fn, new_fn, 1))
print("[v263] buildTypeResolutionCaches guest early exit")
