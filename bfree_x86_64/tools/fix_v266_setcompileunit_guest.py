#!/usr/bin/env python3
"""Guest: setCompileUnit(m_compiledData) instead of m_document (QUrl::fileName crash)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old = """    if (!m_document.isNull()) {
        // Compile component
        compile(typeNameCache, &resolvedTypeCache, dependencyHasher);
        if (isError())
            return;
        else
            setCompileUnit(m_document);
    }"""

new = """    if (!m_document.isNull()) {
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

if old not in tt:
    if "setCompileUnit(m_compiledData)" in tt:
        print("[v266] setCompileUnit guest patch already applied")
        raise SystemExit(0)
    raise SystemExit("[v266] setCompileUnit anchor missing")

td.write_text(tt.replace(old, new, 1))
print("[v266] setCompileUnit uses m_compiledData on guest")
