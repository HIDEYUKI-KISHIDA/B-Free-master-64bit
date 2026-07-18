#!/usr/bin/env python3
from pathlib import Path

p = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
text = p.read_text()

# Drop any prior export block(s)
while True:
    start = text.find("extern \"C\" QThreadData *bfree_guest_qt_current_thread_data")
    if start < 0:
        break
    end = text.find("\n\n", text.find("return ed->processEvents", start))
    if end < 0:
        end = len(text)
    else:
        end += 2
    text = text[:start] + text[end:]

export_hook = """
extern "C" QThreadData *bfree_guest_qt_current_thread_data(void)
{
    return get_thread_data();
}

extern "C" void bfree_guest_qt_set_current_thread_data(QThreadData *data)
{
    set_thread_data(data);
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
    QAbstractEventDispatcher *ed = data->eventDispatcher.loadRelaxed();
    if (!ed)
        return 0;
    return ed->processEvents(QEventLoop::AllEvents) ? 1 : 0;
}

"""
marker = "QT_END_NAMESPACE\n"
if marker not in text:
    raise SystemExit("QT_END_NAMESPACE missing")
text = text.replace(marker, export_hook + marker, 1)
p.write_text(text)
print("exports inserted before QT_END_NAMESPACE")
