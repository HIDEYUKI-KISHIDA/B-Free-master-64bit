#!/usr/bin/env python3
"""Guest: load QML source from embedded bytes, not SourceCodeData::readAll."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "bfree_guest_mvp_shell_qml_data" in tt:
    print("[v241] embed source already patched")
    raise SystemExit(0)

extern = """
#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" const char *bfree_guest_mvp_shell_qml_data(void);
extern "C" int bfree_guest_mvp_shell_qml_size(void);
#endif

"""
if "bfree_guest_mvp_shell_qml_data" not in tt:
    tt = tt.replace("#include", extern + "#include", 1)

old = """    QString sourceError;
    const QString source = m_backupSourceCode.readAll(&sourceError);
    if (!sourceError.isEmpty()) {
        setError(sourceError);
        return false;
    }"""

new = """    QString sourceError;
#if defined(BFREE_GUEST_FIXED_STACK)
    const char *embed = bfree_guest_mvp_shell_qml_data();
    const int embedSize = bfree_guest_mvp_shell_qml_size();
    const QString source = (embed != nullptr && embedSize > 0)
            ? QString::fromUtf8(embed, embedSize)
            : QString();
    if (source.isEmpty()) {
        setError(QLatin1String("GuestMvpShell embed empty"));
        return false;
    }
#else
    const QString source = m_backupSourceCode.readAll(&sourceError);
    if (!sourceError.isEmpty()) {
        setError(sourceError);
        return false;
    }
#endif"""

if old not in tt:
    raise SystemExit("[v241] readAll anchor missing")
tt = tt.replace(old, new, 1)
td.write_text(tt)
print("[v241] typedata embed qml source")
