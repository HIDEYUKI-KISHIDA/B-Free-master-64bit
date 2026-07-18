from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
te = td.read_text()

if "typedata_data_recv" in te:
    print("[v229] typeloader heartbeats already patched")
    raise SystemExit(0)

extern = """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" void bfree_guest_qv4_heartbeat(const char *);
#endif

"""
if 'extern "C" void bfree_guest_qv4_heartbeat' not in te:
    te = te.replace("#include", extern + "#include", 1)

patches = [
    (
        "void QQmlTypeData::dataReceived(const SourceCodeData &data)\n{",
        "void QQmlTypeData::dataReceived(const SourceCodeData &data)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_data_recv");\n'
        "#endif\n",
    ),
    (
        "bool QQmlTypeData::loadFromSource()\n{",
        "bool QQmlTypeData::loadFromSource()\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_load_src");\n'
        "#endif\n",
    ),
    (
        "    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {",
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_qmlgen_enter");\n'
        "#endif\n"
        "    if (!compiler.generateFromQml(source, finalUrlString(), m_document.data())) {",
    ),
    (
        "        return false;\n    }\n    return true;\n}\n\nvoid QQmlTypeData::restoreIR",
        "        return false;\n    }\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_qmlgen_ok");\n'
        "#endif\n"
        "    return true;\n}\n\nvoid QQmlTypeData::restoreIR",
    ),
    (
        "void QQmlTypeData::continueLoadFromIR()\n{",
        "void QQmlTypeData::continueLoadFromIR()\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_cont_ir");\n'
        "#endif\n",
    ),
    (
        "void QQmlTypeData::resolveTypes()\n{",
        "void QQmlTypeData::resolveTypes()\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_resolve");\n'
        "#endif\n",
    ),
    (
        "void QQmlTypeData::compile(const QQmlRefPointer<QQmlTypeNameCache> &typeNameCache,\n"
        "                           QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache,\n"
        "                           const QV4::CompiledData::DependentTypesHasher &dependencyHasher)\n{",
        "void QQmlTypeData::compile(const QQmlRefPointer<QQmlTypeNameCache> &typeNameCache,\n"
        "                           QV4::CompiledData::ResolvedTypeReferenceMap *resolvedTypeCache,\n"
        "                           const QV4::CompiledData::DependentTypesHasher &dependencyHasher)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_compile");\n'
        "#endif\n",
    ),
    (
        "    auto compilationUnit = compiler.compile();\n",
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typedata_compile_done");\n'
        "#endif\n"
        "    auto compilationUnit = compiler.compile();\n",
    ),
]

for old, new in patches:
    if old not in te:
        raise SystemExit(f"[v229] anchor missing: {old[:60]!r}")
    te = te.replace(old, new, 1)

td.write_text(te)
print("[v229] typeloader heartbeats")
