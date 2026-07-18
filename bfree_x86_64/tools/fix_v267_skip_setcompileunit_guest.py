#!/usr/bin/env python3
"""Guest: skip setCompileUnit after compile (QUrl::fileName crash in setCompileUnit)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_skip_setcompileunit" in tt:
    print("[v267] setCompileUnit skip already patched")
    raise SystemExit(0)

old = """    if (!m_document.isNull()) {
        // Compile component
        compile(typeNameCache, &resolvedTypeCache, dependencyHasher);
        if (isError())
            return;
#if defined(BFREE_GUEST_FIXED_STACK)
        else if (m_compiledData)
            setCompileUnit(m_compiledData);
#else
        else
            setCompileUnit(m_document);
#endif
    }"""

new = """    if (!m_document.isNull()) {
        // Compile component
        compile(typeNameCache, &resolvedTypeCache, dependencyHasher);
        if (isError())
            return;
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_skip_setcompileunit");
#else
        else
            setCompileUnit(m_document);
#endif
    }"""

if old not in tt:
    raise SystemExit("[v267] setCompileUnit block anchor missing")

td.write_text(tt.replace(old, new, 1))
print("[v267] skip setCompileUnit on guest after compile")
