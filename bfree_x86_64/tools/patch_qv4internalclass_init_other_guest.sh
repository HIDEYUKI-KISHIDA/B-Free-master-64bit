#!/usr/bin/env bash
# Guest InternalClass::init(other): set internalClass self-pointer (missing early-return path).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
text = path.read_text()

old = """    protoId = other->protoId;
    return;
#endif
    Base::init();
    new (&propertyTable) PropertyHash(other->propertyTable);"""

new = """    protoId = other->protoId;
    if (eng)
        internalClass.set(eng, this);
    return;
#endif
    Base::init();
    new (&propertyTable) PropertyHash(other->propertyTable);"""

if new in text:
    print("[init_other_guest] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[init_other_guest] ok")
else:
    raise SystemExit("[init_other_guest] anchor missing")
PY
