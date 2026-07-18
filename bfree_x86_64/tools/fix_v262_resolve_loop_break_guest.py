#!/usr/bin/env python3
"""Guest: single type ref in resolveTypes loop (break after first store)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old = """        g_guestResolvedValid = g_guestResolvedTypeIndex >= 0;
        bfree_guest_qv4_heartbeat("typedata_resolve_store_ok");
#else
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
#endif
    }"""

new = """        g_guestResolvedValid = g_guestResolvedTypeIndex >= 0;
        bfree_guest_qv4_heartbeat("typedata_resolve_store_ok");
        break;
#else
        m_resolvedTypes.insert(unresolvedRef.key(), ref);
#endif
    }"""

if "typedata_resolve_store_ok" in tt and "break;" in tt.split("typedata_resolve_store_ok")[1].split("#else")[0]:
    print("[v262] resolveTypes guest break already patched")
    raise SystemExit(0)

if old not in tt:
    raise SystemExit("[v262] store_ok anchor missing")

td.write_text(tt.replace(old, new, 1))
print("[v262] break after first guest type resolve")
