#!/usr/bin/env python3
"""Fix broken #if nesting in resolveTypes (v248); guest runs type loop."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

broken = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");
    bfree_guest_qv4_heartbeat("typedata_resolve_scripts_skip");
#else
    // Add any imported scripts to our resolved set
    const auto resolvedScripts = m_importCache->resolvedScripts();
    for (const QQmlImports::ScriptReference &script : resolvedScripts) {
        QQmlRefPointer<QQmlScriptBlob> blob = typeLoader()->getScript(script.location);
        addDependency(blob.data());

        ScriptReference ref;
        //ref.location = ...
        if (!script.qualifier.isEmpty())
        {
            ref.qualifier = script.qualifier + QLatin1Char('.') + script.nameSpace;
            // Add a reference to the enclosing namespace
            m_namespaces.insert(script.qualifier);
        } else {
            ref.qualifier = script.nameSpace;
        }

        ref.script = blob;
        m_scripts << ref;
    }

#if !defined(BFREE_GUEST_FIXED_STACK)
    // Lets handle resolved composite singleton types
    const auto resolvedCompositeSingletons = m_importCache->resolvedCompositeSingletons();
    for (const QQmlImports::CompositeSingletonReference &csRef : resolvedCompositeSingletons) {
        TypeReference ref;
        QString typeName;
        if (!csRef.prefix.isEmpty()) {
            typeName = csRef.prefix + QLatin1Char('.') + csRef.typeName;
            // Add a reference to the enclosing namespace
            m_namespaces.insert(csRef.prefix);
        } else {
            typeName = csRef.typeName;
        }

        QTypeRevision version = csRef.version;
        if (!resolveType(typeName, version, ref, -1, -1, true, QQmlType::CompositeSingletonType))
            return;

        if (ref.type.isCompositeSingleton()) {
            ref.typeData = typeLoader()->getType(ref.type.sourceUrl());
            if (ref.typeData->isWaiting() || m_waitingOnMe.contains(ref.typeData.data())) {
                qCDebug(lcCycle) << "Possible cyclic dependency detected between"
                                 << ref.typeData->urlString() << "and" << urlString();
                continue;
            }
            addDependency(ref.typeData.data());
            ref.prefix = csRef.prefix;

            m_compositeSingletons << ref;
        }
    }
#endif
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_typeref");
#endif

    for (QV4::CompiledData::TypeReferenceMap::ConstIterator unresolvedRef = m_typeReferences.constBegin(), end = m_typeReferences.constEnd();
         unresolvedRef != end; ++unresolvedRef) {

        TypeReference ref; // resolved reference

        const bool reportErrors = unresolvedRef->errorWhenNotFound;

        QTypeRevision version;

        const QString name = stringAt(unresolvedRef.key());
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
#endif

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),
                         unresolvedRef->location.column(), reportErrors,
                         QQmlType::AnyRegistrationType, selfReferenceDetection) && reportErrors)
            return;

        if (ref.type.isComposite() && !ref.selfReference) {
            ref.typeData = typeLoader()->getType(ref.type.sourceUrl());
            addDependency(ref.typeData.data());
        }
        if (ref.type.isInlineComponentType()) {
            QUrl containingTypeUrl = ref.type.sourceUrl();
            containingTypeUrl.setFragment(QString());
            if (!containingTypeUrl.isEmpty()) {
                auto typeData = typeLoader()->getType(containingTypeUrl);
                if (typeData.data() != this) {
                    ref.typeData = typeData;
                    addDependency(typeData.data());
                }
            }
        }

        ref.version = version;
        ref.location = unresolvedRef->location;
        ref.needsCreation = unresolvedRef->needsCreation;
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif
#endif"""

fixed = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");
    bfree_guest_qv4_heartbeat("typedata_resolve_scripts_skip");
#else
    // Add any imported scripts to our resolved set
    const auto resolvedScripts = m_importCache->resolvedScripts();
    for (const QQmlImports::ScriptReference &script : resolvedScripts) {
        QQmlRefPointer<QQmlScriptBlob> blob = typeLoader()->getScript(script.location);
        addDependency(blob.data());

        ScriptReference ref;
        //ref.location = ...
        if (!script.qualifier.isEmpty())
        {
            ref.qualifier = script.qualifier + QLatin1Char('.') + script.nameSpace;
            // Add a reference to the enclosing namespace
            m_namespaces.insert(script.qualifier);
        } else {
            ref.qualifier = script.nameSpace;
        }

        ref.script = blob;
        m_scripts << ref;
    }
#endif

#if !defined(BFREE_GUEST_FIXED_STACK)
    // Lets handle resolved composite singleton types
    const auto resolvedCompositeSingletons = m_importCache->resolvedCompositeSingletons();
    for (const QQmlImports::CompositeSingletonReference &csRef : resolvedCompositeSingletons) {
        TypeReference ref;
        QString typeName;
        if (!csRef.prefix.isEmpty()) {
            typeName = csRef.prefix + QLatin1Char('.') + csRef.typeName;
            // Add a reference to the enclosing namespace
            m_namespaces.insert(csRef.prefix);
        } else {
            typeName = csRef.typeName;
        }

        QTypeRevision version = csRef.version;
        if (!resolveType(typeName, version, ref, -1, -1, true, QQmlType::CompositeSingletonType))
            return;

        if (ref.type.isCompositeSingleton()) {
            ref.typeData = typeLoader()->getType(ref.type.sourceUrl());
            if (ref.typeData->isWaiting() || m_waitingOnMe.contains(ref.typeData.data())) {
                qCDebug(lcCycle) << "Possible cyclic dependency detected between"
                                 << ref.typeData->urlString() << "and" << urlString();
                continue;
            }
            addDependency(ref.typeData.data());
            ref.prefix = csRef.prefix;

            m_compositeSingletons << ref;
        }
    }
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_typeref");
#endif

    for (QV4::CompiledData::TypeReferenceMap::ConstIterator unresolvedRef = m_typeReferences.constBegin(), end = m_typeReferences.constEnd();
         unresolvedRef != end; ++unresolvedRef) {

        TypeReference ref; // resolved reference

        const bool reportErrors = unresolvedRef->errorWhenNotFound;

        QTypeRevision version;

        const QString name = stringAt(unresolvedRef.key());
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
#endif

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),
                         unresolvedRef->location.column(), reportErrors,
                         QQmlType::AnyRegistrationType, selfReferenceDetection) && reportErrors)
            return;

#if !defined(BFREE_GUEST_FIXED_STACK)
        if (ref.type.isComposite() && !ref.selfReference) {
            ref.typeData = typeLoader()->getType(ref.type.sourceUrl());
            addDependency(ref.typeData.data());
        }
        if (ref.type.isInlineComponentType()) {
            QUrl containingTypeUrl = ref.type.sourceUrl();
            containingTypeUrl.setFragment(QString());
            if (!containingTypeUrl.isEmpty()) {
                auto typeData = typeLoader()->getType(containingTypeUrl);
                if (typeData.data() != this) {
                    ref.typeData = typeData;
                    addDependency(typeData.data());
                }
            }
        }
#endif

        ref.version = version;
        ref.location = unresolvedRef->location;
        ref.needsCreation = unresolvedRef->needsCreation;
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif"""

if "typedata_resolve_composite_skip" in tt:
    print("[v249] resolveTypes if-structure already fixed")
    raise SystemExit(0)

if broken not in tt:
    raise SystemExit("[v249] broken resolveTypes block not found")
tt = tt.replace(broken, fixed, 1)
td.write_text(tt)
print("[v249] resolveTypes if-structure fixed + skip composite getType on guest")
