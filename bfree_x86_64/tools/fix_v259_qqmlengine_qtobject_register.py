#!/usr/bin/env python3
"""Guest: register only QtObject (skip full qml_register_types_QML — crashes in memset)."""
from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlengine.cpp")
te = eng.read_text()

if "qmlRegisterType<QtObject>" in te and "qqml_register_qtobject" in te:
    print("[v259] minimal QtObject register already patched")
    raise SystemExit(0)

include = """#if defined(BFREE_GUEST_FIXED_STACK)
#include <private/qqmlbuiltinfunctions_p.h>
#endif

"""
if "qqmlbuiltinfunctions_p.h" not in te:
    te = te.replace(
        "#include <QtQml/qqml.h>",
        "#include <QtQml/qqml.h>\n" + include,
        1,
    )

old = """    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_enter");
#endif
        // Register builtins (QtQml/QtObject — real symbol from libQt6Qml.a)
        qml_register_types_QML();
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_done");
#endif
"""

new = """    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_enter");
        qmlRegisterType<QtObject>("QtQml", 2, 15, "QtObject");
        bfree_guest_qv4_heartbeat("qqml_register_qtobject");
        bfree_guest_qv4_heartbeat("qqml_register_done");
#else
        // Register builtins
        qml_register_types_QML();
#endif
"""

if old not in te:
    # v227 skip block still present?
    old2 = """    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_skip");
#else
        // Register builtins
        qml_register_types_QML();
#endif
"""
    if old2 in te:
        te = te.replace(old2, new, 1)
    else:
        raise SystemExit("[v259] qqmlengine register block missing")
else:
    te = te.replace(old, new, 1)

eng.write_text(te)
print("[v259] qqmlengine registers QtObject only on guest")
