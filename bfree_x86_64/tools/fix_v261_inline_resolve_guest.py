#!/usr/bin/env python3
"""Guest: inline QtObject resolve in resolveTypes loop (skip resolveType call + QString)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_resolve_inline_ok" in tt:
    print("[v261] inline resolve already patched")
    raise SystemExit(0)

old = """        const QString name = stringAt(unresolvedRef.key());
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
#endif

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),
                         unresolvedRef->location.column(), reportErrors,
                         QQmlType::AnyRegistrationType, selfReferenceDetection) && reportErrors)
            return;"""

new = """#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_resolve_type");
        bfree_guest_qv4_heartbeat("typedata_resolve_qtobject");
        version = QTypeRevision::fromVersion(2, 15);
        {
            QQmlType t = QQmlMetaType::qmlType(&_ZN8QtObject16staticMetaObjectE);
            if (!t.isValid()) {
                bfree_guest_qv4_heartbeat("typedata_resolve_fail");
                if (reportErrors) {
                    QQmlError error;
                    error.setDescription(QStringLiteral("Guest inline resolve QtObject failed"));
                    setError(QList<QQmlError>() << error);
                }
                return;
            }
            ref.type = t;
        }
        bfree_guest_qv4_heartbeat("typedata_resolve_inline_ok");
#else
        const QString name = stringAt(unresolvedRef.key());

        bool *selfReferenceDetection = unresolvedRef->needsCreation ? nullptr : &ref.selfReference;

        if (!resolveType(name, version, ref, unresolvedRef->location.line(),
                         unresolvedRef->location.column(), reportErrors,
                         QQmlType::AnyRegistrationType, selfReferenceDetection) && reportErrors)
            return;
#endif"""

if old not in tt:
    raise SystemExit("[v261] resolveTypes loop anchor missing")

td.write_text(tt.replace(old, new, 1))
print("[v261] inline QtObject resolve in resolveTypes loop")
