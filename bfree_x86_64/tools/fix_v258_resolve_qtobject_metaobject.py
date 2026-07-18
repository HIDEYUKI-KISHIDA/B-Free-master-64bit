#!/usr/bin/env python3
"""Guest resolveType: QtObject via staticMetaObject (no QString lookup / no stub register)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_resolve_qtobject" in tt and "struct QMetaObject;" in tt:
    print("[v258] QtObject metaobject resolve already patched")
    raise SystemExit(0)

extern = """#if defined(BFREE_GUEST_FIXED_STACK)
struct QMetaObject;
extern const QMetaObject _ZN8QtObject16staticMetaObjectE;
#endif

"""
if "_ZN8QtObject16staticMetaObjectE" not in tt:
    anchor = "#include <private/qqmlmetatype_p.h>"
    if anchor not in tt:
        raise SystemExit("[v258] qqmlmetatype include missing")
    tt = tt.replace(anchor, anchor + "\n" + extern, 1)

# Remove stale v254 lookup extern if present
tt = tt.replace(
    """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" int bfree_guest_lookup_qml_type_id(const char *name);
#endif

""",
    "",
)
tt = tt.replace(
    """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" int bfree_guest_lookup_qml_type_id(const char *name);
extern const QMetaObject _ZN8QtObject16staticMetaObjectE;
#endif

""",
    extern,
)

old = """#if defined(BFREE_GUEST_FIXED_STACK)
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

new = """#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    (void)typeName;
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
    bfree_guest_qv4_heartbeat("typedata_resolve_qtobject");
    QQmlType t = QQmlMetaType::qmlType(&_ZN8QtObject16staticMetaObjectE);
    if (t.isValid()) {
        ref.type = t;
        version = guestVersion;
        bfree_guest_qv4_heartbeat("typedata_resolve_ok");
        return true;
    }
    bfree_guest_qv4_heartbeat("typedata_resolve_fail");
    if (reportErrors) {
        QQmlError error;
        error.setDescription(QStringLiteral("Guest resolveType QtObject failed"));
        setError(QList<QQmlError>() << error);
    }
    return false;
#else"""

if old not in tt:
    raise SystemExit("[v258] v254 resolveType guest block missing")

td.write_text(tt.replace(old, new, 1))
print("[v258] resolveType via QtObject::staticMetaObject")
