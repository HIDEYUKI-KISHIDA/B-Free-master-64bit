#!/usr/bin/env python3
"""Guest: short-circuit resolveTypes; resolve Window via QQmlMetaType directly."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_resolve_guest" in tt:
    print("[v247] resolveTypes guest short-circuit already patched")
    raise SystemExit(0)

anchor_start = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");
#endif

    // Add any imported scripts to our resolved set
    const auto resolvedScripts = m_importCache->resolvedScripts();"""

guest_block = """#if defined(BFREE_GUEST_FIXED_STACK)
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

if anchor_start not in tt:
    raise SystemExit("[v247] resolveTypes start anchor missing")
tt = tt.replace(anchor_start, guest_block, 1)

anchor_end = """        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }

    // ### this allows enums to work without explicit import or instantiation of the type"""

end_repl = """        m_resolvedTypes.insert(unresolvedRef.key(), ref);
    }
#endif

    // ### this allows enums to work without explicit import or instantiation of the type"""

if anchor_end not in tt:
    raise SystemExit("[v247] resolveTypes end anchor missing")
tt = tt.replace(anchor_end, end_repl, 1)

# Remove duplicate typedata_resolve_done at function end if present
tt = tt.replace(
    """    if (!m_implicitImportLoaded)
        loadImplicitImport();
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif
}""",
    """    if (!m_implicitImportLoaded)
        loadImplicitImport();
}""",
    1,
)

if "#include <private/qqmlmetatype_p.h>" not in tt:
    tt = tt.replace(
        "#include <private/qqmltypeloaderqmldircontent_p.h>",
        "#include <private/qqmltypeloaderqmldircontent_p.h>\n#include <private/qqmlmetatype_p.h>",
        1,
    )

td.write_text(tt)
print("[v247] resolveTypes guest short-circuit")
