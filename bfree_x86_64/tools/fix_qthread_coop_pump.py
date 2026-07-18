#!/usr/bin/env python3
"""Enhance B-Free cooperative Qt event pumps in qthread_unix.cpp."""
from pathlib import Path

p = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
text = p.read_text()

old_pump = """extern "C" int bfree_guest_qt_coop_pump_thread(void *qthread_ptr)
{
    QThread *thr = reinterpret_cast<QThread *>(qthread_ptr);
    QThreadData *const saved = get_thread_data();
    QThreadData *const data = QThreadData::get2(thr);
    auto restore = qScopeGuard([&] { set_thread_data(saved); });
    if (!data)
        return 0;
    set_thread_data(data);
    QAbstractEventDispatcher *ed = data->eventDispatcher.loadRelaxed();
    if (!ed)
        return 0;
    return ed->processEvents(QEventLoop::AllEvents) ? 1 : 0;
}
"""

new_pump = """static int bfree_guest_qt_pump_dispatcher(QAbstractEventDispatcher *ed)
{
    int n = 0;
    if (!ed)
        return 0;
    for (int i = 0; i < 2; ++i) {
        if (ed->processEvents(QEventLoop::AllEvents))
            ++n;
        if (ed->processEvents(QEventLoop::WaitForMoreEvents | QEventLoop::ExcludeSocketNotifiers))
            ++n;
    }
    return n;
}

extern "C" int bfree_guest_qt_coop_pump_main(void)
{
    if (!QCoreApplication::instance())
        return 0;
    QThreadData *const saved = get_thread_data();
    QThread *const mainThr = QCoreApplicationPrivate::mainThread();
    QThreadData *const md = mainThr ? QThreadData::get2(mainThr) : saved;
    auto restore = qScopeGuard([&] { set_thread_data(saved); });
    if (!md)
        return 0;
    set_thread_data(md);
    return bfree_guest_qt_pump_dispatcher(md->eventDispatcher.loadRelaxed()) > 0 ? 1 : 0;
}

extern "C" int bfree_guest_qt_coop_pump_thread(void *qthread_ptr)
{
    QThread *thr = reinterpret_cast<QThread *>(qthread_ptr);
    QThreadData *const saved = get_thread_data();
    QThreadData *const data = QThreadData::get2(thr);
    auto restore = qScopeGuard([&] { set_thread_data(saved); });
    if (!data)
        return 0;
    set_thread_data(data);
    return bfree_guest_qt_pump_dispatcher(data->eventDispatcher.loadRelaxed()) > 0 ? 1 : 0;
}
"""

if old_pump not in text:
    raise SystemExit("coop_pump_thread block not found")
text = text.replace(old_pump, new_pump, 1)
if "qeventloop.h" not in text:
    text = text.replace("#include \"qthread_p.h\"\n", "#include \"qthread_p.h\"\n#include <QtCore/qeventloop.h>\n", 1)
p.write_text(text)
print("enhanced coop pumps in", p)
