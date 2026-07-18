#!/usr/bin/env python3
"""Guest: short-circuit addLibraryImport for QtQuick (Qt 6.8 importDatabase API)."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

if "addlib_import_guest" in te:
    print("[v243] addLibraryImport guest already patched")
    raise SystemExit(0)

if 'extern "C" void bfree_guest_qv4_heartbeat' not in te:
    te = te.replace(
        "#include",
        '#if defined(BFREE_GUEST_FIXED_STACK)\nextern "C" void bfree_guest_qv4_heartbeat(const char *);\n#endif\n\n#include',
        1,
    )

old = """bool QQmlTypeLoader::Blob::addLibraryImport(const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    QQmlImportDatabase *importDatabase = typeLoader()->importDatabase();"""

new = """bool QQmlTypeLoader::Blob::addLibraryImport(const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (import->uri == QLatin1String("QtQuick")
            || import->uri == QLatin1String("QtQuick.Window")) {
        bfree_guest_qv4_heartbeat("addlib_import_guest");
        if (QQmlMetaType::typeModule(import->uri, import->version)
                || QQmlMetaType::qmlRegisterModuleTypes(import->uri)
                || QQmlMetaType::latestModuleVersion(import->uri).isValid()) {
            if (m_importCache->addLibraryImport(
                        typeLoader(), import->uri, import->qualifier, import->version,
                        QString(), QString(), import->flags, import->precedence, errors)
                        .isValid()) {
                return true;
            }
        }
        return false;
    }
#endif

    QQmlImportDatabase *importDatabase = typeLoader()->importDatabase();"""

if old not in te:
    raise SystemExit("[v243] addLibraryImport anchor missing")
te = te.replace(old, new, 1)
tl.write_text(te)
print("[v243] addLibraryImport guest short-circuit")
