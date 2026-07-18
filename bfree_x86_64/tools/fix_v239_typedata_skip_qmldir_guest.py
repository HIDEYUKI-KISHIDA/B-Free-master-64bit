#!/usr/bin/env python3
"""Guest: skip qmldir/isLocal block in continueLoadFromIR (QUrl::toString faults)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old = """    // For remote URLs, we don't delay the loading of the implicit import
    // because the loading probably requires an asynchronous fetch of the
    // qmldir (so we can't load it just in time).
#if defined(BFREE_GUEST_FIXED_STACK)
    if (true) {
        QUrl qmldirUrl = bfree_guest_static_qml_qurl().resolved(QUrl(QLatin1String("qmldir")));
#else
    if (!finalUrl().scheme().isEmpty()) {
        QUrl qmldirUrl = finalUrl().resolved(QUrl(QLatin1String("qmldir")));
#endif
        if (!QQmlImports::isLocal(qmldirUrl)) {
            if (!loadImplicitImport())
                return;
            // This qmldir is for the implicit import
            auto implicitImport = std::make_shared<PendingImport>();
            implicitImport->uri = QLatin1String(".");
            implicitImport->version = QTypeRevision();
            QList<QQmlError> errors;

            if (!fetchQmldir(qmldirUrl, implicitImport, 1, &errors)) {
                setError(errors);
                return;
            }
        }
    }

    QList<QQmlError> errors;"""

new = """    // For remote URLs, we don't delay the loading of the implicit import
    // because the loading probably requires an asynchronous fetch of the
    // qmldir (so we can't load it just in time).
#if !defined(BFREE_GUEST_FIXED_STACK)
    if (!finalUrl().scheme().isEmpty()) {
        QUrl qmldirUrl = finalUrl().resolved(QUrl(QLatin1String("qmldir")));
        if (!QQmlImports::isLocal(qmldirUrl)) {
            if (!loadImplicitImport())
                return;
            // This qmldir is for the implicit import
            auto implicitImport = std::make_shared<PendingImport>();
            implicitImport->uri = QLatin1String(".");
            implicitImport->version = QTypeRevision();
            QList<QQmlError> errors;

            if (!fetchQmldir(qmldirUrl, implicitImport, 1, &errors)) {
                setError(errors);
                return;
            }
        }
    }
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_cont_ir_imports");
#endif
    QList<QQmlError> errors;"""

if "typedata_cont_ir_imports" in tt:
    print("[v239] qmldir skip already patched")
    raise SystemExit(0)

if old not in tt:
    raise SystemExit("[v239] continueLoadFromIR qmldir anchor missing")
tt = tt.replace(old, new, 1)

# Heartbeat after addImport loop
old_end = """            setError(error);
            return;
        }
    }
}

void QQmlTypeData::allDependenciesDone()"""

new_end = """            setError(error);
            return;
        }
    }
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_cont_ir_done");
#endif
}

void QQmlTypeData::allDependenciesDone()"""

if old_end in tt:
    tt = tt.replace(old_end, new_end, 1)

td.write_text(tt)
print("[v239] skip guest qmldir block + import heartbeats")
