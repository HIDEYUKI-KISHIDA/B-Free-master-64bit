#!/usr/bin/env bash
# Guest: QThreadPrivate::start after startingUp — skip setCurrentThreadName/run (#UD), keep emit started.
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
text = path.read_text()

skip_all = """        /* guest: skip setCurrentThreadName/emit/run — sync coop start #UD in .rodata (RIP~0x35BB009) */
        (void)thr;
    });"""

emit_only = """        /* guest: skip setCurrentThreadName/run (prctl #UD); keep emit started for QQmlEngine wait */
#if (defined(Q_OS_LINUX) || defined(Q_OS_DARWIN) || defined(Q_OS_QNX))
        (void)thr;
#else
        if (Q_LIKELY(thr->d_func()->objectName.isEmpty()))
            setCurrentThreadName(thr->metaObject()->className());
        else
            setCurrentThreadName(std::exchange(thr->d_func()->objectName, {}).toLocal8Bit());
#endif
        emit thr->started(QThread::QPrivateSignal());
    });"""

if emit_only in text:
    print("[patch_qthread_start_minimal_guest] emit-only already applied")
elif skip_all in text:
    path.write_text(text.replace(skip_all, emit_only, 1))
    print("[patch_qthread_start_minimal_guest] emit-only ok")
else:
    raise SystemExit("[patch_qthread_start_minimal_guest] start body anchor missing")
PY
