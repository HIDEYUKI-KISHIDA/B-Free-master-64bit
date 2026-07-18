#!/usr/bin/env python3
"""Guest: load qrc QML via QResource bytes; skip QFile case check hang."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

if "typeloader_qrc_bytes" in te:
    print("[v233] qrc bytes path already patched")
    raise SystemExit(0)

if "#include <QResource>" not in te:
    te = te.replace("#include <QtCore/qfile.h>", "#include <QtCore/qfile.h>\n#include <QResource>", 1)
    print("[v233] ok (QResource include)")

old = """        const QString fileName = QQmlFile::urlToLocalFileOrQrc(blob->m_url);
        if (!QQml_isFileCaseCorrect(fileName)) {
            blob->setError(QLatin1String("File name case mismatch"));
            return;
        }

        blob->m_data.setProgress(1.f);"""

new = """        const QString fileName = QQmlFile::urlToLocalFileOrQrc(blob->m_url);
#if defined(BFREE_GUEST_FIXED_STACK)
        if (fileName.startsWith(QLatin1Char(':'))) {
            const QResource res(fileName);
            if (res.isValid() && res.data() != nullptr && res.size() > 0) {
                bfree_guest_qv4_heartbeat("typeloader_qrc_bytes");
                blob->m_data.setProgress(1.f);
                setData(blob, QByteArray(reinterpret_cast<const char *>(res.data()),
                                         int(res.size())));
                return;
            }
            bfree_guest_qv4_heartbeat("typeloader_qrc_miss");
        }
#endif
        if (!QQml_isFileCaseCorrect(fileName)) {
            blob->setError(QLatin1String("File name case mismatch"));
            return;
        }

        blob->m_data.setProgress(1.f);"""

if old not in te:
    raise SystemExit("[v233] loadThread anchor missing")
te = te.replace(old, new, 1)
tl.write_text(te)
print("[v233] typeloader qrc bytes done")
