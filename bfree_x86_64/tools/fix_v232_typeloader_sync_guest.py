#!/usr/bin/env python3
"""Guest: run TypeLoader on engine thread; skip cross-thread progress (deadlock)."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

if "typeloader_doload_inline" in te:
    print("[v232] typeloader sync already patched")
    raise SystemExit(0)

# Disable load-thread assert when compiling inline on engine thread.
assert_patch = """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" void bfree_guest_qv4_heartbeat(const char *);
#undef ASSERT_LOADTHREAD
#define ASSERT_LOADTHREAD() do {} while (0)
#endif
"""
if "#undef ASSERT_LOADTHREAD" not in te:
    te = te.replace(
        "#if defined(BFREE_GUEST_FIXED_STACK)\nextern \"C\" void bfree_guest_qv4_heartbeat(const char *);\n#endif",
        assert_patch,
        1,
    )
    print("[v232] ok (ASSERT_LOADTHREAD guest noop)")

old_doload = """    blob->startLoading();

    if (m_thread->isThisThread()) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typeloader_doload_sync");
#endif
        unlock();
        loader.loadThread(this, blob);
        lock();
    } else if (mode == Asynchronous) {"""

new_doload = """    blob->startLoading();

#if defined(BFREE_GUEST_FIXED_STACK)
    if (!m_thread->isThisThread()) {
        bfree_guest_qv4_heartbeat("typeloader_doload_inline");
        unlock();
        loader.loadThread(this, blob);
        lock();
        return;
    }
#endif

    if (m_thread->isThisThread()) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typeloader_doload_sync");
#endif
        unlock();
        loader.loadThread(this, blob);
        lock();
    } else if (mode == Asynchronous) {"""

if old_doload not in te:
    raise SystemExit("[v232] doLoad anchor missing")
te = te.replace(old_doload, new_doload, 1)
print("[v232] ok (doLoad inline)")

old_prog = """        blob->m_data.setProgress(1.f);
        if (blob->m_data.isAsync())
            m_thread->callDownloadProgressChanged(blob, 1.);

#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typeloader_setdata_file");
#endif
        setData(blob, fileName);"""

new_prog = """        blob->m_data.setProgress(1.f);
#if !defined(BFREE_GUEST_FIXED_STACK)
        if (blob->m_data.isAsync())
            m_thread->callDownloadProgressChanged(blob, 1.);
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("typeloader_setdata_file");
#endif
        setData(blob, fileName);"""

if old_prog not in te:
    raise SystemExit("[v232] loadThread progress anchor missing")
te = te.replace(old_prog, new_prog, 1)
print("[v232] ok (skip callDownloadProgressChanged)")

tl.write_text(te)
print("[v232] typeloader sync done")
