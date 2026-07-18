#!/usr/bin/env python3
"""Guest: adopt precompiled qmlcache unit instead of QQmlTypeCompiler when types are complete."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_adopt_precached_unit" in tt:
    print("[v273] precached skip typecompile already patched")
    raise SystemExit(0)

old_v266 = """    if (!m_document.isNull()) {
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

old_v267 = """    if (!m_document.isNull()) {
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

old_plain = """    if (!m_document.isNull()) {
        Q_ASSERT(verifyCaches);
        // Compile component
        compile(typeNameCache, &resolvedTypeCache, dependencyHasher);
        if (isError())
            return;
    }"""

new = """    if (!m_document.isNull()) {
#if defined(BFREE_GUEST_FIXED_STACK)
        if (m_document->javaScriptCompilationUnit
            && m_document->javaScriptCompilationUnit->unitData()
            && !(m_document->javaScriptCompilationUnit->unitData()->flags
                 & QV4::CompiledData::Unit::PendingTypeCompilation)) {
            bfree_guest_qv4_heartbeat("typedata_adopt_precached_unit");
            m_compiledData = m_document->javaScriptCompilationUnit;
            const QQmlError precachedError =
                    createTypeAndPropertyCaches(typeNameCache, resolvedTypeCache);
            if (precachedError.isValid()) {
                setError(precachedError);
                return;
            }
        } else
#endif
        {
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
        }
    }"""

if old_v267 in tt:
    tt = tt.replace(old_v267, new, 1)
elif old_v266 in tt:
    tt = tt.replace(old_v266, new, 1)
elif old_plain in tt:
    tt = tt.replace(old_plain, new, 1)
else:
    raise SystemExit("[v273] done() compile block anchor missing")

td.write_text(tt)
print("[v273] precached unit adopt + skip TypeCompiler when possible")
