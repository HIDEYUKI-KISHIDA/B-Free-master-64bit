#!/usr/bin/env bash
# Guest: skip QMetaTypeCustomRegistry ctor aliases.insert (QHash null node #PF CR2=0x10).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/kernel/qmetatype.cpp")
text = path.read_text()

old = """#if QT_VERSION < QT_VERSION_CHECK(7, 0, 0) && !defined(QT_BOOTSTRAPPED)
    QMetaTypeCustomRegistry()
    {
        /* qfloat16 was neither a builtin, nor unconditionally registered
          in QtCore in Qt <= 6.2.
          Inserting it as an alias ensures that a QMetaType::id call
          will get the correct built-in type-id (the interface pointers
          might still not match, but we already deal with that case.
        */
        aliases.insert("qfloat16", QtPrivate::qMetaTypeInterfaceForType<qfloat16>());
    }
#endif"""

new = """#if QT_VERSION < QT_VERSION_CHECK(7, 0, 0) && !defined(QT_BOOTSTRAPPED)
    QMetaTypeCustomRegistry()
    {
        /* guest: skip aliases.insert during Q_GLOBAL_STATIC init (QHash node null #PF) */
    }
#endif"""

if new in text:
    print("[patch_qmetatype_registry_guest] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[patch_qmetatype_registry_guest] ok")
else:
    raise SystemExit("[patch_qmetatype_registry_guest] anchor missing")
PY
