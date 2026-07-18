#!/usr/bin/env python3
"""Guest: QtQml-only imports; lookup via qml_register_types_QML."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old_imports = """    if (!guestAddKnownImport("QtQuick", 2, 15)
            || !guestAddKnownImport("QtQuick.Window", 2, 15)) {"""

new_imports = """    if (!guestAddKnownImport("QtQml", 2, 15)) {"""

if new_imports.split("{")[0] in tt and "QtQuick.Window" not in tt.split("guestAddKnownImport")[1][:200]:
    print("[v256] typedata imports already QtQml-only")
elif old_imports in tt:
    tt = tt.replace(old_imports, new_imports, 1)
    td.write_text(tt)
    print("[v256] typedata imports -> QtQml only")
else:
    raise SystemExit("[v256] typedata import anchor missing")
