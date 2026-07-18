#!/usr/bin/env python3
"""Guest: skip done() dependency checks using finalUrl/QUrl::path before build cache."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_done_skip_deps" in tt:
    print("[v264] done() guest skip already patched")
    raise SystemExit(0)

old = """    if (isError())
        return;

    // Check all script dependencies for errors
    for (int ii = 0; ii < m_scripts.size(); ++ii) {"""

new = """    if (isError())
        return;

#if !defined(BFREE_GUEST_FIXED_STACK)
    // Check all script dependencies for errors
    for (int ii = 0; ii < m_scripts.size(); ++ii) {"""

if old not in tt:
    raise SystemExit("[v264] done() start anchor missing")
tt = tt.replace(old, new, 1)

old2 = """    if (m_document)
        setupICs(m_document, &m_inlineComponentData, finalUrl(), m_compiledData);
    else
        setupICs(m_compiledData, &m_inlineComponentData, finalUrl(), m_compiledData);

    QV4::CompiledData::ResolvedTypeReferenceMap resolvedTypeCache;"""

new2 = """    if (m_document)
        setupICs(m_document, &m_inlineComponentData, finalUrl(), m_compiledData);
    else
        setupICs(m_compiledData, &m_inlineComponentData, finalUrl(), m_compiledData);
#else
    bfree_guest_qv4_heartbeat("typedata_done_skip_deps");
#endif

    QV4::CompiledData::ResolvedTypeReferenceMap resolvedTypeCache;"""

if old2 not in tt:
    raise SystemExit("[v264] done() setupICs anchor missing")
tt = tt.replace(old2, new2, 1)

td.write_text(tt)
print("[v264] done() guest skip dependency checks")
