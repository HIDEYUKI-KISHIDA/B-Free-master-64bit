#!/usr/bin/env python3
"""Guest resolveType: lookup Window via QQuickWindow::staticMetaObject (no qml_register_types)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "QQuickWindow::staticMetaObject" in tt:
    print("[v252] Window metaobject resolve already patched")
    raise SystemExit(0)

if "Guest resolveType via MetaType failed" not in tt:
    raise SystemExit("[v252] v250 resolveType guest block missing")

include = "#include <private/qqmlmetatype_p.h>"
if include not in tt:
    raise SystemExit("[v252] qqmlmetatype include missing")

tt = tt.replace(
    include,
    include + "\n#include <QtQuick/qquickwindow.h>",
    1,
)

old = """#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    QQmlMetaType::qmlRegisterModuleTypes(QLatin1String("QtQuick"));
    QQmlMetaType::qmlRegisterModuleTypes(QLatin1String("QtQuick.Window"));
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
    static const char *modules[] = {"QtQuick.Window", "QtQuick", nullptr};
    for (int mi = 0; modules[mi]; ++mi) {
        QQmlType t = QQmlMetaType::qmlType(
                QHashedStringRef(typeName),
                QHashedStringRef(QLatin1String(modules[mi])),
                guestVersion);
        if (t.isValid()) {
            ref.type = t;
            version = guestVersion;
            bfree_guest_qv4_heartbeat("typedata_resolve_ok");
            return true;
        }
    }
    QQmlType anyModule = QQmlMetaType::qmlType(
            QHashedStringRef(typeName), QHashedStringRef(), guestVersion);
    if (anyModule.isValid()) {
        ref.type = anyModule;
        version = guestVersion;
        bfree_guest_qv4_heartbeat("typedata_resolve_ok");
        return true;
    }
    bfree_guest_qv4_heartbeat("typedata_resolve_fail");
    if (reportErrors) {
        QQmlError error;
        error.setDescription(QStringLiteral("Guest resolveType via MetaType failed"));
        setError(QList<QQmlError>() << error);
    }
    return false;
#else"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
    if (typeName == QLatin1String("Window")) {
        QQmlType t = QQmlMetaType::qmlType(&QQuickWindow::staticMetaObject);
        if (t.isValid()) {
            ref.type = t;
            version = guestVersion;
            bfree_guest_qv4_heartbeat("typedata_resolve_ok");
            return true;
        }
    }
    static const char *modules[] = {"QtQuick.Window", "QtQuick", nullptr};
    for (int mi = 0; modules[mi]; ++mi) {
        QQmlType t = QQmlMetaType::qmlType(
                QHashedStringRef(typeName),
                QHashedStringRef(QLatin1String(modules[mi])),
                guestVersion);
        if (t.isValid()) {
            ref.type = t;
            version = guestVersion;
            bfree_guest_qv4_heartbeat("typedata_resolve_ok");
            return true;
        }
    }
    bfree_guest_qv4_heartbeat("typedata_resolve_fail");
    if (reportErrors) {
        QQmlError error;
        error.setDescription(QStringLiteral("Guest resolveType failed"));
        setError(QList<QQmlError>() << error);
    }
    return false;
#else"""

if old not in tt:
    raise SystemExit("[v252] resolveType guest block anchor missing")
tt = tt.replace(old, new, 1)
td.write_text(tt)
print("[v252] Window via QQuickWindow::staticMetaObject")
