#!/usr/bin/env python3
"""Guest resolveType: delegate type lookup to guest_link_compat (no QtQuick in Qml)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

tt = tt.replace("#include <QtQuick/qquickwindow.h>\n", "")
tt = tt.replace(
    """#if defined(BFREE_GUEST_FIXED_STACK)
extern const QMetaObject _ZN12QQuickWindow16staticMetaObjectE;
#endif

""",
    "",
)

extern_h = """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" int bfree_guest_lookup_qml_type_id(const char *name);
#endif

"""

if "bfree_guest_lookup_qml_type_id(nameUtf8" in tt:
    print("[v254] resolveType callback already patched")
    raise SystemExit(0)

old = """#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
    if (typeName == QLatin1String("Window")) {
        QQmlType t = QQmlMetaType::qmlType(&_ZN12QQuickWindow16staticMetaObjectE);
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

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
    const QByteArray nameUtf8 = typeName.toUtf8();
    const int typeId = bfree_guest_lookup_qml_type_id(nameUtf8.constData());
    if (typeId >= 0) {
        ref.type = QQmlMetaType::qmlTypeById(typeId);
        if (ref.type.isValid()) {
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
    # try v250 block without Window metaobject
    old2 = old.replace("""    if (typeName == QLatin1String("Window")) {
        QQmlType t = QQmlMetaType::qmlType(&_ZN12QQuickWindow16staticMetaObjectE);
        if (t.isValid()) {
            ref.type = t;
            version = guestVersion;
            bfree_guest_qv4_heartbeat("typedata_resolve_ok");
            return true;
        }
    }
    """, "")
    if old2 in tt:
        tt = tt.replace(old2, new, 1)
    else:
        raise SystemExit("[v254] resolveType guest block missing")
else:
    tt = tt.replace(old, new, 1)

td.write_text(tt)
print("[v254] resolveType via bfree_guest_lookup_qml_type_id")
