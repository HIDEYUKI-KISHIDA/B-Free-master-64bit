#!/usr/bin/env python3
"""Guest: use hardcoded Latin1 import URIs instead of string-table PendingImport."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "guestAddKnownImport" in tt:
    print("[v240] guest known imports already patched")
    raise SystemExit(0)

old = """    QList<QQmlError> errors;

    for (const QV4::CompiledData::Import *import : std::as_const(m_document->imports)) {
        if (!addImport(import, {}, &errors)) {
            Q_ASSERT(errors.size());

            // We're only interested in the chronoligically last error. The previous
            // errors might be from unsuccessfully trying to load a module from the
            // resource file system.
            QQmlError error = errors.first();
            error.setUrl(m_importCache->baseUrl());
            error.setLine(qmlConvertSourceCoordinate<quint32, int>(import->location.line()));
            error.setColumn(qmlConvertSourceCoordinate<quint32, int>(import->location.column()));
            setError(error);
            return;
        }
    }"""

new = """    QList<QQmlError> errors;

#if defined(BFREE_GUEST_FIXED_STACK)
    auto guestAddKnownImport = [&](const char *uri, int major, int minor) -> bool {
        bfree_guest_qv4_heartbeat("typedata_addimport");
        auto imp = std::make_shared<PendingImport>();
        imp->uri = QString::fromLatin1(uri);
        imp->type = QV4::CompiledData::Import::ImportLibrary;
        imp->version = QTypeRevision::fromVersion(major, minor);
        return addImport(imp, &errors);
    };
    if (!guestAddKnownImport("QtQuick", 2, 15)
            || !guestAddKnownImport("QtQuick.Window", 2, 15)) {
        if (!errors.isEmpty()) {
            QQmlError error = errors.first();
            error.setUrl(m_importCache->baseUrl());
            setError(errors);
        } else {
            setError(QLatin1String("GuestMvpShell import resolution failed"));
        }
        return;
    }
#else
    for (const QV4::CompiledData::Import *import : std::as_const(m_document->imports)) {
        if (!addImport(import, {}, &errors)) {
            Q_ASSERT(errors.size());

            // We're only interested in the chronoligically last error. The previous
            // errors might be from unsuccessfully trying to load a module from the
            // resource file system.
            QQmlError error = errors.first();
            error.setUrl(m_importCache->baseUrl());
            error.setLine(qmlConvertSourceCoordinate<quint32, int>(import->location.line()));
            error.setColumn(qmlConvertSourceCoordinate<quint32, int>(import->location.column()));
            setError(error);
            return;
        }
    }
#endif"""

if old not in tt:
    raise SystemExit("[v240] addImport loop anchor missing")
tt = tt.replace(old, new, 1)
td.write_text(tt)
print("[v240] guest known QtQuick imports")
