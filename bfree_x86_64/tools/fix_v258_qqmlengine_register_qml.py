#!/usr/bin/env python3
"""Guest: call real qml_register_types_QML in QQmlEngine init (stub removed in v258)."""
from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlengine.cpp")
te = eng.read_text()

old = """    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_skip");
#else
        // Register builtins
        qml_register_types_QML();
#endif
"""

new = """    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_enter");
#endif
        // Register builtins (QtQml/QtObject — real symbol from libQt6Qml.a)
        qml_register_types_QML();
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_done");
#endif
"""

if "qqml_register_done" in te:
    print("[v258] qqmlengine register patch already applied")
    raise SystemExit(0)

if old not in te:
    raise SystemExit("[v258] qqml_register_skip block missing")

eng.write_text(te.replace(old, new, 1))
print("[v258] qqmlengine calls qml_register_types_QML on guest")
