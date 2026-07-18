from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

if "typeloader_gettype" in te:
    print("[v230] typeloader entry heartbeats already patched")
    raise SystemExit(0)

extern = """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" void bfree_guest_qv4_heartbeat(const char *);
#endif

"""
if 'extern "C" void bfree_guest_qv4_heartbeat' not in te:
    te = te.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + extern, 1)

patches = [
    (
        "QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QUrl &unNormalizedUrl, Mode mode)\n{",
        "QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QUrl &unNormalizedUrl, Mode mode)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typeloader_gettype");\n'
        "#endif\n",
    ),
    (
        "QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QByteArray &data, const QUrl &url, Mode mode)\n{",
        "QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QByteArray &data, const QUrl &url, Mode mode)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typeloader_gettype_data");\n'
        "#endif\n",
    ),
    (
        "void QQmlTypeLoader::loadThread(const QQmlDataBlob::Ptr &blob)\n{",
        "void QQmlTypeLoader::loadThread(const QQmlDataBlob::Ptr &blob)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typeloader_loadthread");\n'
        "#endif\n",
    ),
    (
        "        setData(blob, fileName);\n",
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '        bfree_guest_qv4_heartbeat("typeloader_setdata_file");\n'
        "#endif\n"
        "        setData(blob, fileName);\n",
    ),
    (
        "void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QQmlDataBlob::SourceCodeData &d)\n{",
        "void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QQmlDataBlob::SourceCodeData &d)\n{\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '    bfree_guest_qv4_heartbeat("typeloader_setdata");\n'
        "#endif\n",
    ),
    (
        "    if (m_thread->isThisThread()) {\n        unlock();\n        loader.loadThread(this, blob);",
        "    if (m_thread->isThisThread()) {\n"
        "#if defined(BFREE_GUEST_FIXED_STACK)\n"
        '        bfree_guest_qv4_heartbeat("typeloader_doload_sync");\n'
        "#endif\n"
        "        unlock();\n        loader.loadThread(this, blob);",
    ),
]

for old, new in patches:
    if old not in te:
        raise SystemExit(f"[v230] anchor missing: {old[:60]!r}")
    te = te.replace(old, new, 1)

tl.write_text(te)
print("[v230] typeloader entry heartbeats")
