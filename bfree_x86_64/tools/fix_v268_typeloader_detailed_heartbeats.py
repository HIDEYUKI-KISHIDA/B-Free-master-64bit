#!/usr/bin/env python3
"""Guest: add detailed heartbeats in TypeLoader dataReceived/compile path to identify hang."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

if "typeloader_datareceived_entry" in te:
    print("[v268] typeloader detailed heartbeats already patched")
    raise SystemExit(0)

# Add heartbeat at setData entry
old_setdata = """void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QQmlDataBlob::SourceCodeData &d)
{
    ASSERT_LOADTHREAD();

    Q_TRACE_SCOPE(QQmlCompiling, blob->url());
    QQmlCompilingProfiler prof(profiler(), blob.data());

    blob->m_inCallback = true;

    blob->dataReceived(d);"""

new_setdata = """void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QQmlDataBlob::SourceCodeData &d)
{
    ASSERT_LOADTHREAD();

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typeloader_setdata_entry");
#endif

    Q_TRACE_SCOPE(QQmlCompiling, blob->url());
    QQmlCompilingProfiler prof(profiler(), blob.data());

    blob->m_inCallback = true;

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typeloader_datareceived_call");
#endif
    blob->dataReceived(d);

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("typeloader_datareturned");
#endif"""

if old_setdata not in te:
    raise SystemExit("[v268] setData anchor missing")
te = te.replace(old_setdata, new_setdata, 1)
print("[v268] ok (setData heartbeats)")

tl.write_text(te)
print("[v268] typeloader detailed heartbeats done")
