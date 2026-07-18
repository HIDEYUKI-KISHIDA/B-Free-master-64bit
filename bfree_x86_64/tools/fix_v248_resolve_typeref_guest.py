#!/usr/bin/env python3
"""Guest: skip resolvedScripts/composite loops; keep resolveType for type refs."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_resolve_scripts_skip" in tt:
    print("[v248] resolveTypes partial skip already patched")
    raise SystemExit(0)

if "typedata_resolve_guest" not in tt:
    raise SystemExit("[v248] v247 guest block missing — apply v247 first")

# Replace v247 full short-circuit with partial skip + normal type loop
old_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");
    bfree_guest_qv4_heartbeat("typedata_resolve_guest");
    for (QV4::CompiledData::TypeReferenceMap::ConstIterator unresolvedRef = m_typeReferences.constBegin(),
            end = m_typeReferences.constEnd();
         unresolvedRef != end; ++unresolvedRef) {
        TypeReference ref;
        QTypeRevision version;
        const QString name = stringAt(unresolvedRef.key());
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
        QQmlType t = QQmlMetaType::qmlType(
                QHashedStringRef(name),
                QHashedStringRef(QLatin1String("QtQuick.Window")),
                version);
        if (!t.isValid()) {
            t = QQmlMetaType::qmlType(
                    QHashedStringRef(name),
                    QHashedStringRef(QLatin1String("QtQuick")),
                    version);
        }
        if (!t.isValid()) {
            QQmlError err;
            err.setDescription(QStringLiteral("Guest type resolve failed: ") + name);
            setError(QList<QQmlError>() << err);
            return;
        }
        ref.type = t;
        ref.version = version;
        ref.location = unresolvedRef->location;
        ref.needsCreation = unresolvedRef->needsCreation;
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#else
    // Add any imported scripts to our resolved set
    const auto resolvedScripts = m_importCache->resolvedScripts();"""

new_guest = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");
    bfree_guest_qv4_heartbeat("typedata_resolve_scripts_skip");
#else
    // Add any imported scripts to our resolved set
    const auto resolvedScripts = m_importCache->resolvedScripts();"""

if old_guest not in tt:
    raise SystemExit("[v248] v247 guest block anchor missing")

tt = tt.replace(old_guest, new_guest, 1)

old_composite = """    // Lets handle resolved composite singleton types
    const auto resolvedCompositeSingletons = m_importCache->resolvedCompositeSingletons();
    for (const QQmlImports::CompositeSingletonReference &csRef : resolvedCompositeSingletons) {"""

new_composite = """#if !defined(BFREE_GUEST_FIXED_STACK)
    // Lets handle resolved composite singleton types
    const auto resolvedCompositeSingletons = m_importCache->resolvedCompositeSingletons();
    for (const QQmlImports::CompositeSingletonReference &csRef : resolvedCompositeSingletons) {"""

if new_composite.split("for (const QQmlImports::CompositeSingletonReference")[0] in tt:
    # already partially patched
    pass
elif old_composite not in tt:
    raise SystemExit("[v248] composite anchor missing")
else:
    tt = tt.replace(old_composite, new_composite, 1)

# Close composite #if before type ref loop
old_before_typeref = """            m_compositeSingletons << ref;
        }
    }

    for (QV4::CompiledData::TypeReferenceMap::ConstIterator unresolvedRef = m_typeReferences.constBegin(), end = m_typeReferences.constEnd();"""

new_before_typeref = """            m_compositeSingletons << ref;
        }
    }
#endif
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_typeref");
#endif

    for (QV4::CompiledData::TypeReferenceMap::ConstIterator unresolvedRef = m_typeReferences.constBegin(), end = m_typeReferences.constEnd();"""

if "typedata_resolve_typeref" not in tt:
    if old_before_typeref not in tt:
        raise SystemExit("[v248] typeref anchor missing")
    tt = tt.replace(old_before_typeref, new_before_typeref, 1)

# Heartbeat inside type loop
old_resolve_call = """        const QString name = stringAt(unresolvedRef.key());

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),"""

new_resolve_call = """        const QString name = stringAt(unresolvedRef.key());
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
#endif

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),"""

if "typedata_resolve_type" not in tt or tt.count("typedata_resolve_type") == 1:
    if old_resolve_call not in tt:
        raise SystemExit("[v248] resolveType call anchor missing")
    tt = tt.replace(old_resolve_call, new_resolve_call, 1)

# done heartbeat before #endif closing else branch
old_end = """        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#endif

    // ### this allows enums"""

new_end = """        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif
#endif

    // ### this allows enums"""

if "typedata_resolve_done" in tt and tt.count("typedata_resolve_done") >= 1:
    # replace if only one at end of guest block from v247 removal
    if old_end not in tt:
        # maybe already has done heartbeat elsewhere
        if new_end.split("typedata_resolve_done")[0] not in tt:
            pass
    else:
        tt = tt.replace(old_end, new_end, 1)
elif old_end in tt:
    tt = tt.replace(old_end, new_end, 1)
else:
    raise SystemExit("[v248] resolveTypes end #endif anchor missing")

td.write_text(tt)
print("[v248] resolveTypes skip scripts/composite only")
