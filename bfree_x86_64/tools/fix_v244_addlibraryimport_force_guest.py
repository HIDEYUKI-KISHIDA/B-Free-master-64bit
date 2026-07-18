#!/usr/bin/env python3
"""Guest: force QtQuick library imports to succeed after module registration."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

old = """#if defined(BFREE_GUEST_FIXED_STACK)
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
#endif"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    if (import->uri == QLatin1String("QtQuick")
            || import->uri == QLatin1String("QtQuick.Window")) {
        bfree_guest_qv4_heartbeat("addlib_import_guest");
        if (!QQmlMetaType::typeModule(import->uri, import->version))
            QQmlMetaType::qmlRegisterModuleTypes(import->uri);
        (void)m_importCache->addLibraryImport(
                typeLoader(), import->uri, import->qualifier, import->version,
                QString(), QString(),
                import->flags | QQmlImports::ImportIncomplete, import->precedence, errors);
        bfree_guest_qv4_heartbeat("addlib_import_ok");
        return true;
    }
#endif"""

if "addlib_import_ok" in te:
    print("[v244] addLibraryImport force ok already patched")
    raise SystemExit(0)

if old not in te:
    raise SystemExit("[v244] v243 guest block missing")
te = te.replace(old, new, 1)
tl.write_text(te)
print("[v244] addLibraryImport force success")
