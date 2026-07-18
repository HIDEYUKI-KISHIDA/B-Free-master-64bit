#!/usr/bin/env python3
"""Patch Qt qthread_unix.cpp for B-Free cooperative pthread (no finish(), TLS restore)."""
from pathlib import Path

p = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
text = p.read_text()

# Replace entire start() with cooperative version (guest Qt is B-Free only).
start_marker = "void *QThreadPrivate::start(void *arg)\n{"
finish_marker = "void QThreadPrivate::finish(void *arg)\n{"
i0 = text.find(start_marker)
i1 = text.find(finish_marker)
if i0 < 0 or i1 < 0 or i1 <= i0:
    raise SystemExit("start()/finish() markers not found")

coop_start = r'''void *QThreadPrivate::start(void *arg)
{
#ifdef PTHREAD_CANCEL_DISABLE
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
#endif
    /* B-Free cooperative pthread: same OS thread; restore creator TLS; no finish(). */
    QThreadData *const bfree_coop_saved_tls = get_thread_data();
    auto bfree_coop_restore_tls = qScopeGuard([&] { set_thread_data(bfree_coop_saved_tls); });

    terminate_on_exception([&] {
        QThread *thr = reinterpret_cast<QThread *>(arg);
        QThreadData *data = QThreadData::get2(thr);

        {
            QMutexLocker locker(&thr->d_func()->mutex);

            if (thr->d_func()->priority & ThreadPriorityResetFlag) {
                thr->d_func()->setPriority(
                        QThread::Priority(thr->d_func()->priority & ~ThreadPriorityResetFlag));
            }

            set_thread_data(data);
            data->ref();
            data->quitNow = thr->d_func()->exited;
        }

        data->ensureEventDispatcher();
        data->eventDispatcher.loadRelaxed()->startingUp();

#if (defined(Q_OS_LINUX) || defined(Q_OS_DARWIN) || defined(Q_OS_QNX))
        if (Q_LIKELY(thr->d_func()->objectName.isEmpty()))
            setCurrentThreadName(thr->metaObject()->className());
        else
            setCurrentThreadName(std::exchange(thr->d_func()->objectName, {}).toLocal8Bit());
#endif

        emit thr->started(QThread::QPrivateSignal());
#ifdef PTHREAD_CANCEL_DISABLE
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
        pthread_testcancel();
#endif
        thr->run();
    });

    return nullptr;
}

'''

text = text[:i0] + coop_start + text[i1:]

if "qeventloop.h" not in text:
    text = text.replace("#include \"qthread_p.h\"\n", "#include \"qthread_p.h\"\n#include <QtCore/qeventloop.h>\n", 1)

export_hook = """
extern \"C\" QThreadData *bfree_guest_qt_current_thread_data(void)
{
    using namespace QT_NAMESPACE;
    return get_thread_data();
}

extern \"C\" void bfree_guest_qt_set_current_thread_data(QThreadData *data)
{
    using namespace QT_NAMESPACE;
    set_thread_data(data);
}

extern \"C\" int bfree_guest_qt_coop_pump_thread(void *qthread_ptr)
{
    using namespace QT_NAMESPACE;
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
if "bfree_guest_qt_coop_pump_thread" not in text:
    if marker not in text:
        raise SystemExit("QT_END_NAMESPACE missing")
    # Remove old export block if present (inside or outside namespace)
    old = "extern \"C\" QThreadData *bfree_guest_qt_current_thread_data"
    if old in text:
        j0 = text.find(old)
        j1 = text.find("QT_END_NAMESPACE", j0)
        if j1 > j0:
            text = text[:j0] + text[j1:]
        else:
            j1 = text.find("\n\n", j0)
            text = text[:j0] + text[j1 + 2 if j1 > 0 else j0:]
    text = text.replace(marker, marker + export_hook, 1)

p.write_text(text)
print("patched cooperative start (no finish):", p)
