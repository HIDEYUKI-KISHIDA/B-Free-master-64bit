#!/usr/bin/env python3
"""Guest: resolveType via QQmlMetaType only; avoid import-cache QStringHash crash."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_resolve_ok" in tt and "Guest resolveType via MetaType" in tt:
    print("[v250] resolveType guest bypass already patched")
    raise SystemExit(0)

old = """bool QQmlTypeData::resolveType(const QString &typeName, QTypeRevision &version,
                               TypeReference &ref, int lineNumber, int columnNumber,
                               bool reportErrors, QQmlType::RegistrationType registrationType,
                               bool *typeRecursionDetected)
{
    QQmlImportNamespace *typeNamespace = nullptr;
    QList<QQmlError> errors;

    bool typeFound = m_importCache->resolveType(
            typeLoader(), typeName, &ref.type, &version, &typeNamespace, &errors, registrationType,
            typeRecursionDetected);"""

new = """bool QQmlTypeData::resolveType(const QString &typeName, QTypeRevision &version,
                               TypeReference &ref, int lineNumber, int columnNumber,
                               bool reportErrors, QQmlType::RegistrationType registrationType,
                               bool *typeRecursionDetected)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    (void)lineNumber;
    (void)columnNumber;
    (void)registrationType;
    (void)typeRecursionDetected;
    QQmlMetaType::qmlRegisterModuleTypes(QLatin1String("QtQuick"));
    QQmlMetaType::qmlRegisterModuleTypes(QLatin1String("QtQuick.Window"));
    const QTypeRevision guestVersion = QTypeRevision::fromVersion(2, 15);
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
    QQmlType anyModule = QQmlMetaType::qmlType(
            QHashedStringRef(typeName), QHashedStringRef(), guestVersion);
    if (anyModule.isValid()) {
        ref.type = anyModule;
        version = guestVersion;
        bfree_guest_qv4_heartbeat("typedata_resolve_ok");
        return true;
    }
    bfree_guest_qv4_heartbeat("typedata_resolve_fail");
    if (reportErrors) {
        QQmlError error;
        error.setDescription(QStringLiteral("Guest resolveType via MetaType failed"));
        setError(QList<QQmlError>() << error);
    }
    return false;
#else
    QQmlImportNamespace *typeNamespace = nullptr;
    QList<QQmlError> errors;

    bool typeFound = m_importCache->resolveType(
            typeLoader(), typeName, &ref.type, &version, &typeNamespace, &errors, registrationType,
            typeRecursionDetected);"""

if old not in tt:
    raise SystemExit("[v250] resolveType anchor missing")

tt = tt.replace(old, new, 1)

close_anchor = """    return true;
}

void QQmlTypeData::scriptImported("""

close_new = """    return true;
#endif
}

void QQmlTypeData::scriptImported("""

# Only add #endif before scriptImported if not already there
idx = tt.find("void QQmlTypeData::scriptImported(")
before = tt[:idx]
if "#endif\n}\n\nvoid QQmlTypeData::scriptImported(" not in tt:
    # find end of resolveType - the return true before scriptImported
    old_end = """        setError(errors);
        return false;
    }

    return true;
}

void QQmlTypeData::scriptImported("""
    new_end = """        setError(errors);
        return false;
    }

    return true;
#endif
}

void QQmlTypeData::scriptImported("""
    if old_end not in tt:
        raise SystemExit("[v250] resolveType end anchor missing")
    tt = tt.replace(old_end, new_end, 1)

if "#include <private/qqmlmetatype_p.h>" not in tt:
    tt = tt.replace(
        "#include <private/qqmltypeloaderqmldircontent_p.h>",
        "#include <private/qqmltypeloaderqmldircontent_p.h>\n#include <private/qqmlmetatype_p.h>",
        1,
    )

td.write_text(tt)
print("[v250] resolveType guest MetaType bypass")
