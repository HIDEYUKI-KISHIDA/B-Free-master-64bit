#!/usr/bin/env python3
"""Use extern QQuickWindow::staticMetaObject — no QtQuick include in Qml."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

tt = tt.replace("#include <QtQuick/qquickwindow.h>\n", "")

if "bfree_guest_qquick_window_metaobject" in tt:
    print("[v253] extern staticMetaObject already patched")
    raise SystemExit(0)

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
extern const QMetaObject QQuickWindow_staticMetaObject;
namespace {
struct BfreeGuestQQuickWindowMetaObject {
    BfreeGuestQQuickWindowMetaObject()
    {
        QQuickWindow_staticMetaObject = *reinterpret_cast<const QMetaObject *>(
                reinterpret_cast<void (*[])()>([]() {}) /* placeholder */);
    }
};
}
#endif

"""

# Simpler: just extern from guest - use asm label or mangled name
extern_block = """
#if defined(BFREE_GUEST_FIXED_STACK)
extern const QMetaObject _ZN12QQuickWindow16staticMetaObjectE;
#endif

"""

if extern_block.strip() not in tt:
    tt = tt.replace(
        "#include <private/qqmlmetatype_p.h>",
        "#include <private/qqmlmetatype_p.h>\n" + extern_block,
        1,
    )

old = """    if (typeName == QLatin1String("Window")) {
        QQmlType t = QQmlMetaType::qmlType(&QQuickWindow::staticMetaObject);"""

new = """    if (typeName == QLatin1String("Window")) {
        QQmlType t = QQmlMetaType::qmlType(&_ZN12QQuickWindow16staticMetaObjectE);"""

if old not in tt:
    raise SystemExit("[v253] Window metaobject anchor missing")
tt = tt.replace(old, new, 1)
td.write_text(tt)
print("[v253] extern QQuickWindow staticMetaObject (no QtQuick include)")
