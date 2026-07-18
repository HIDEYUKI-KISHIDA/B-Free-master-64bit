#!/usr/bin/env python3
"""Guest: add detailed heartbeats in QQmlTypeData dataReceived/compile path to identify hang."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_datareceived_entry" in tt:
    print("[v269] typedata detailed heartbeats already patched")
    raise SystemExit(0)

# Add heartbeat at dataReceived entry
old_datareceived = """void QQmlTypeData::dataReceived(const SourceCodeData &data)
{
    assertTypeLoaderThread();

    m_backupSourceCode = data;

    if (tryLoadFromDiskCache())
        return;"""

new_datareceived = """void QQmlTypeData::dataReceived(const SourceCodeData &data)
{
    assertTypeLoaderThread();

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_datareceived_entry");
#endif

    m_backupSourceCode = data;

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_try_diskcache");
#endif
    if (tryLoadFromDiskCache()) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_diskcache_loaded");
#endif
        return;
    }

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_diskcache_miss");
#endif"""

if old_datareceived not in tt:
    raise SystemExit("[v269] dataReceived anchor missing")
tt = tt.replace(old_datareceived, new_datareceived, 1)
print("[v269] ok (dataReceived entry heartbeats)")

# Add heartbeat at loadFromSource
old_loadsource = """bool QQmlTypeData::loadFromSource()
{
    assertTypeLoaderThread();

    m_document.reset(
            new QmlIR::Document(urlString(), finalUrlString(), m_typeLoader->isDebugging()));
    m_document->jsModule.sourceTimeStamp = m_backupSourceCode.sourceTimeStamp();
    QmlIR::IRBuilder compiler;

    QString sourceError;
    const QString source = m_backupSourceCode.readAll(&sourceError);"""

new_loadsource = """bool QQmlTypeData::loadFromSource()
{
    assertTypeLoaderThread();

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_loadfromsource_entry");
#endif

    m_document.reset(
            new QmlIR::Document(urlString(), finalUrlString(), m_typeLoader->isDebugging()));
    m_document->jsModule.sourceTimeStamp = m_backupSourceCode.sourceTimeStamp();
    QmlIR::IRBuilder compiler;

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_readall");
#endif
    QString sourceError;
    const QString source = m_backupSourceCode.readAll(&sourceError);"""

if old_loadsource not in tt:
    raise SystemExit("[v269] loadFromSource anchor missing")
tt = tt.replace(old_loadsource, new_loadsource, 1)
print("[v269] ok (loadFromSource heartbeats)")

# Add heartbeat at continueLoadFromIR
old_continue = """void QQmlTypeData::continueLoadFromIR()
{
    assertTypeLoaderThread();

    for (auto const& object: m_document->objects) {"""

new_continue = """void QQmlTypeData::continueLoadFromIR()
{
    assertTypeLoaderThread();

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_continueir_entry");
#endif

    for (auto const& object: m_document->objects) {"""

if old_continue not in tt:
    raise SystemExit("[v269] continueLoadFromIR anchor missing")
tt = tt.replace(old_continue, new_continue, 1)
print("[v269] ok (continueLoadFromIR heartbeats)")

# Add heartbeat at import loop
old_imports = """    for (const QV4::CompiledData::Import *import : std::as_const(m_document->imports)) {
        if (!addImport(import, {}, &errors)) {"""

new_imports = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_imports_loop");
#endif
    for (const QV4::CompiledData::Import *import : std::as_const(m_document->imports)) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typedata_addimport");
#endif
        if (!addImport(import, {}, &errors)) {"""

if old_imports not in tt:
    raise SystemExit("[v269] imports anchor missing")
tt = tt.replace(old_imports, new_imports, 1)
print("[v269] ok (imports heartbeats)")

# Add heartbeat at compile entry
old_compile = """void QQmlTypeData::compile(const QQmlRefPointer<QQmlTypeNameCache> &typeNameCache,
                           QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache,
                           const QV4::CompiledData::DependentTypesHasher &dependencyHasher)
{
    assertTypeLoaderThread();

    Q_ASSERT(m_compiledData.isNull());

    const bool typeRecompilation = m_document
            && m_document->javaScriptCompilationUnit
            && m_document->javaScriptCompilationUnit->unitData()
            && (m_document->javaScriptCompilationUnit->unitData()->flags
                & QV4::CompiledData::Unit::PendingTypeCompilation);

    QQmlTypeCompiler compiler(
            typeLoader(), this, m_document.data(), resolvedTypeCache, dependencyHasher);
    auto compilationUnit = compiler.compile();"""

new_compile = """void QQmlTypeData::compile(const QQmlRefPointer<QQmlTypeNameCache> &typeNameCache,
                           QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache,
                           const QV4::CompiledData::DependentTypesHasher &dependencyHasher)
{
    assertTypeLoaderThread();

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_compile_entry");
#endif

    Q_ASSERT(m_compiledData.isNull());

    const bool typeRecompilation = m_document
            && m_document->javaScriptCompilationUnit
            && m_document->javaScriptCompilationUnit->unitData()
            && (m_document->javaScriptCompilationUnit->unitData()->flags
                & QV4::CompiledData::Unit::PendingTypeCompilation);

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_compile_compiler_create");
#endif
    QQmlTypeCompiler compiler(
            typeLoader(), this, m_document.data(), resolvedTypeCache, dependencyHasher);
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typedata_compile_compile_call");
#endif
    auto compilationUnit = compiler.compile();"""

if old_compile not in tt:
    raise SystemExit("[v269] compile anchor missing")
tt = tt.replace(old_compile, new_compile, 1)
print("[v269] ok (compile heartbeats)")

td.write_text(tt)
print("[v269] typedata detailed heartbeats done")
