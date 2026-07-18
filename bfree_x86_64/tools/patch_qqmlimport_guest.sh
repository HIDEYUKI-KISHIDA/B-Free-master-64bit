#!/usr/bin/env bash
# Guest QQmlImportDatabase: qrc-only import paths (skip QLibraryInfo/applicationDirPath readlink hang).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlimport.cpp")
text = path.read_text()

anchor = """    filePluginPath << QLatin1String(".");
    // Search order is:"""

stub_if = """    filePluginPath << QLatin1String(".");
#if defined(BFREE_GUEST_FIXED_STACK)
    addImportPath(QStringLiteral("qrc:/qt/qml"));
    addImportPath(QStringLiteral("qrc:/qt-project.org/imports"));
    return;
#endif
    // Search order is:"""

stub = """    filePluginPath << QLatin1String(".");
    addImportPath(QStringLiteral("qrc:/qt/qml"));
    addImportPath(QStringLiteral("qrc:/qt-project.org/imports"));
    return;
    // Search order is:"""

if stub in text:
    print("[patch_qqmlimport_guest] already applied (unconditional)")
elif stub_if in text:
    text = text.replace(stub_if, stub, 1)
    path.write_text(text)
    print("[patch_qqmlimport_guest] ok (upgraded to unconditional qrc-only)")
elif anchor in text:
    text = text.replace(anchor, stub, 1)
    path.write_text(text)
    print("[patch_qqmlimport_guest] ok (unconditional qrc-only import paths)")
else:
    raise SystemExit("qqmlimport.cpp: anchor missing")
PY
