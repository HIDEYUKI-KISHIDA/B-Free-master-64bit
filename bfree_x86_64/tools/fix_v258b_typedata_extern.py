#!/usr/bin/env python3
"""Fix v258 qqmltypedata.cpp extern block (forward-declare QMetaObject)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

# Remove broken guest extern at top (before includes)
bad = """#if defined(BFREE_GUEST_FIXED_STACK)
#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" int bfree_guest_lookup_qml_type_id(const char *name);
extern const QMetaObject _ZN8QtObject16staticMetaObjectE;
#endif

#endif

"""
if bad in tt:
    tt = tt.replace(bad, "", 1)

good_extern = """#if defined(BFREE_GUEST_FIXED_STACK)
struct QMetaObject;
extern const QMetaObject _ZN8QtObject16staticMetaObjectE;
#endif

"""

if "struct QMetaObject;" not in tt:
    anchor = "#include <private/qqmlmetatype_p.h>"
    if anchor not in tt:
        raise SystemExit("[v258b] qqmlmetatype include missing")
    tt = tt.replace(anchor, anchor + "\n" + good_extern, 1)

td.write_text(tt)
print("[v258b] qqmltypedata extern fixed")
