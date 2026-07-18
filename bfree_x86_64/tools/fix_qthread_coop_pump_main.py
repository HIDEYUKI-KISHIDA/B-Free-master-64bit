#!/usr/bin/env python3
"""Add bfree_guest_qt_coop_pump_main to patched qthread_unix.cpp if missing."""
from pathlib import Path

p = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
text = p.read_text()
if "bfree_guest_qt_coop_pump_main" in text:
    print("[ok] coop_pump_main already present")
    raise SystemExit(0)

insert = '''
extern "C" int bfree_guest_qt_coop_pump_main(void)
{
    using namespace QT_NAMESPACE;
    if (!QCoreApplication::instance())
        return 0;
    QThreadData *const saved = get_thread_data();
    QThread *const mainThr = QCoreApplicationPrivate::mainThread();
    QThreadData *const md = mainThr ? QThreadData::get2(mainThr) : saved;
    auto restore = qScopeGuard([&] { set_thread_data(saved); });
    if (!md)
        return 0;
    set_thread_data(md);
    QAbstractEventDispatcher *ed = md->eventDispatcher.loadRelaxed();
    if (!ed)
        return 0;
    return ed->processEvents(QEventLoop::AllEvents) ? 1 : 0;
}

'''

needle = 'extern "C" int bfree_guest_qt_coop_pump_thread'
if needle not in text:
    raise SystemExit("coop_pump_thread not found — run patch_qthread_unix_coop.py first")
text = text.replace(needle, insert + needle, 1)
if "qcoreapplication.h" not in text.lower():
    text = text.replace(
        '#include "qthread_p.h"\n',
        '#include "qthread_p.h"\n#include <QtCore/qcoreapplication.h>\n#include <private/qcoreapplication_p.h>\n',
        1,
    )
p.write_text(text)
print("added coop_pump_main to", p)
