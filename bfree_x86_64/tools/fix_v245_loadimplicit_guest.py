#!/usr/bin/env python3
"""Guest: skip loadImplicitImport QUrl path; safe finalUrlString in datablob."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old_implicit = """bool QQmlTypeData::loadImplicitImport()
{
    m_implicitImportLoaded = true; // Even if we hit an error, count as loaded (we'd just keep hitting the error)

    m_importCache->setBaseUrl(finalUrl(), finalUrlString());

    // For local urls, add an implicit import "." as most overridden lookup.
    // This will also trigger the loading of the qmldir and the import of any native
    // types from available plugins.
    QList<QQmlError> implicitImportErrors;
    QString localQmldir;
    m_importCache->addImplicitImport(typeLoader(), &localQmldir, &implicitImportErrors);

    // When loading with QQmlImports::ImportImplicit, the imports are _appended_ to the namespace
    // in the order they are loaded. Therefore, the addImplicitImport above gets the highest
    // precedence. This is in contrast to normal priority imports. Those are _prepended_ in the
    // order they are loaded.
    if (!localQmldir.isEmpty()) {
        const QQmlTypeLoaderQmldirContent qmldir = typeLoader()->qmldirContent(localQmldir);
        const QList<QQmlDirParser::Import> moduleImports
                = QQmlMetaType::moduleImports(qmldir.typeNamespace(), QTypeRevision())
                + qmldir.imports();
        loadDependentImports(moduleImports, QString(), QTypeRevision(),
                             QQmlImportInstance::Implicit + 1, QQmlImports::ImportNoFlag,
                             &implicitImportErrors);
    }

    if (!implicitImportErrors.isEmpty()) {
        setError(implicitImportErrors);
        return false;
    }

    return true;
}"""

new_implicit = """bool QQmlTypeData::loadImplicitImport()
{
    m_implicitImportLoaded = true; // Even if we hit an error, count as loaded (we'd just keep hitting the error)

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_implicit_skip");
    return true;
#else
    m_importCache->setBaseUrl(finalUrl(), finalUrlString());

    // For local urls, add an implicit import "." as most overridden lookup.
    // This will also trigger the loading of the qmldir and the import of any native
    // types from available plugins.
    QList<QQmlError> implicitImportErrors;
    QString localQmldir;
    m_importCache->addImplicitImport(typeLoader(), &localQmldir, &implicitImportErrors);

    // When loading with QQmlImports::ImportImplicit, the imports are _appended_ to the namespace
    // in the order they are loaded. Therefore, the addImplicitImport above gets the highest
    // precedence. This is in contrast to normal priority imports. Those are _prepended_ in the
    // order they are loaded.
    if (!localQmldir.isEmpty()) {
        const QQmlTypeLoaderQmldirContent qmldir = typeLoader()->qmldirContent(localQmldir);
        const QList<QQmlDirParser::Import> moduleImports
                = QQmlMetaType::moduleImports(qmldir.typeNamespace(), QTypeRevision())
                + qmldir.imports();
        loadDependentImports(moduleImports, QString(), QTypeRevision(),
                             QQmlImportInstance::Implicit + 1, QQmlImports::ImportNoFlag,
                             &implicitImportErrors);
    }

    if (!implicitImportErrors.isEmpty()) {
        setError(implicitImportErrors);
        return false;
    }

    return true;
#endif
}"""

if "typedata_implicit_skip" in tt:
    print("[v245] loadImplicitImport already patched")
elif old_implicit not in tt:
    raise SystemExit("[v245] loadImplicitImport anchor missing")
else:
    tt = tt.replace(old_implicit, new_implicit, 1)
    td.write_text(tt)
    print("[v245] loadImplicitImport guest skip")

# resolveTypes heartbeats
tt = td.read_text()
resolve_patch = (
    "    if (!m_implicitImportLoaded && !loadImplicitImport())\n        return;\n",
    "    if (!m_implicitImportLoaded && !loadImplicitImport())\n        return;\n"
    "#if defined(BFREE_GUEST_FIXED_STACK)\n"
    '    bfree_guest_qv4_heartbeat("typedata_resolve_implicit_ok");\n'
    "#endif\n",
)
if "typedata_resolve_implicit_ok" not in tt:
    if resolve_patch[0] not in tt:
        raise SystemExit("[v245] resolveTypes implicit anchor missing")
    tt = tt.replace(resolve_patch[0], resolve_patch[1], 1)

    old_end = """    // ### this allows enums to work without explicit import or instantiation of the type
    if (!m_implicitImportLoaded)
        loadImplicitImport();
}

QQmlError QQmlTypeData::buildTypeResolutionCaches("""
    new_end = """    // ### this allows enums to work without explicit import or instantiation of the type
    if (!m_implicitImportLoaded)
        loadImplicitImport();
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_resolve_done");
#endif
}

QQmlError QQmlTypeData::buildTypeResolutionCaches("""
    if old_end not in tt:
        raise SystemExit("[v245] resolveTypes end anchor missing")
    tt = tt.replace(old_end, new_end, 1)
    td.write_text(tt)
    print("[v245] resolveTypes heartbeats")
else:
    print("[v245] resolveTypes heartbeats already patched")

db = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmldatablob.cpp")
te = db.read_text()

old_final = """QString QQmlDataBlob::finalUrlString() const
{
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = m_finalUrl.toString();

    return m_finalUrlString;
}"""

new_final = """QString QQmlDataBlob::finalUrlString() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = QStringLiteral("qrc:/GuestMvpShell.qml");
#else
    if (m_finalUrlString.isEmpty())
        m_finalUrlString = m_finalUrl.toString();
#endif

    return m_finalUrlString;
}"""

old_url = """QString QQmlDataBlob::urlString() const
{
    if (m_urlString.isEmpty())
        m_urlString = m_url.toString();

    return m_urlString;
}"""

new_url = """QString QQmlDataBlob::urlString() const
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (m_urlString.isEmpty())
        m_urlString = QStringLiteral("qrc:/GuestMvpShell.qml");
#else
    if (m_urlString.isEmpty())
        m_urlString = m_url.toString();
#endif

    return m_urlString;
}"""

if "qrc:/GuestMvpShell.qml" in te and "finalUrlString" in te and "BFREE_GUEST_FIXED_STACK" in te:
    print("[v245] datablob url strings already patched")
else:
    if old_final not in te:
        raise SystemExit("[v245] finalUrlString anchor missing")
    if old_url not in te:
        raise SystemExit("[v245] urlString anchor missing")
    te = te.replace(old_final, new_final, 1)
    te = te.replace(old_url, new_url, 1)
    db.write_text(te)
    print("[v245] datablob safe url strings")
