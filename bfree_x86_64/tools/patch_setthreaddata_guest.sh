#!/usr/bin/env bash
# Guest: minimal setThreadData_helper (postEvent #PF, ConnectionData stack_chk fail).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/kernel/qobject.cpp")
text = path.read_text()

patches = [
    (
        "postEvent skip",
        """    // move posted events
    int eventsMoved = 0;
    for (int i = 0; i < currentData->postEventList.size(); ++i) {
        const QPostEvent &pe = currentData->postEventList.at(i);
        if (!pe.event)
            continue;
        if (pe.receiver == q) {
            // move this post event to the targetList
            targetData->postEventList.addEvent(pe);
            const_cast<QPostEvent &>(pe).event = nullptr;
            ++eventsMoved;
        }
    }
    if (eventsMoved > 0 && targetData->hasEventDispatcher()) {
        targetData->canWait = false;
        targetData->eventDispatcher.loadRelaxed()->wakeUp();
    }""",
        """    // move posted events (guest: skip — postEventList may point at ctor-bump
    // storage that faults during QQmlEngine cooperative thread setup)
    int eventsMoved = 0;
    (void)currentData;
    (void)targetData;
    if (false) {
        for (int i = 0; i < currentData->postEventList.size(); ++i) {
            const QPostEvent &pe = currentData->postEventList.at(i);
            if (!pe.event)
                continue;
            if (pe.receiver == q) {
                targetData->postEventList.addEvent(pe);
                const_cast<QPostEvent &>(pe).event = nullptr;
                ++eventsMoved;
            }
        }
        if (eventsMoved > 0 && targetData->hasEventDispatcher()) {
            targetData->canWait = false;
            targetData->eventDispatcher.loadRelaxed()->wakeUp();
        }
    }""",
    ),
    (
        "minimal affinity v339",
        """    // the current emitting thread shouldn't restore currentSender after calling moveToThread()
    ConnectionData *cd = connections.loadAcquire();
    if (cd) {
        if (cd->currentSender) {
            cd->currentSender->receiverDeleted();
            cd->currentSender = nullptr;
        }

        // adjust the receiverThreadId values in the Connections
        if (cd) {
            auto *c = cd->senders;
            while (c) {
                QObject *r = c->receiver.loadRelaxed();
                if (r) {
                    Q_ASSERT(r == q);
                    targetData->ref();
                    QThreadData *old = c->receiverThreadData.loadRelaxed();
                    if (old)
                        old->deref();
                    c->receiverThreadData.storeRelaxed(targetData);
                }
                c = c->next;
            }
        }

    }

    // set new thread data
    targetData->ref();
    threadData.loadRelaxed()->deref();

    // synchronizes with loadAcquire e.g. in QCoreApplication::postEvent
    threadData.storeRelease(targetData);

    for (int i = 0; i < children.size(); ++i) {
        QObject *child = children.at(i);
        child->d_func()->setThreadData_helper(currentData, targetData, status);
    }
}""",
        """    // guest: minimal affinity (skip ConnectionData walk + children recursion)
    if (!targetData)
        return;
    {
        QThreadData *oldData = threadData.loadRelaxed();
        if (oldData != targetData) {
            targetData->ref();
            if (oldData)
                oldData->deref();
            threadData.storeRelease(targetData);
        }
    }
}""",
    ),
    (
        "minimal affinity already postEvent",
        """    }

    // the current emitting thread shouldn't restore currentSender after calling moveToThread()
    ConnectionData *cd = connections.loadAcquire();
    if (cd) {
        if (cd->currentSender) {
            cd->currentSender->receiverDeleted();
            cd->currentSender = nullptr;
        }

        // adjust the receiverThreadId values in the Connections
        if (cd) {
            auto *c = cd->senders;
            while (c) {
                QObject *r = c->receiver.loadRelaxed();
                if (r) {
                    Q_ASSERT(r == q);
                    targetData->ref();
                    QThreadData *old = c->receiverThreadData.loadRelaxed();
                    if (old)
                        old->deref();
                    c->receiverThreadData.storeRelaxed(targetData);
                }
                c = c->next;
            }
        }

    }

    // set new thread data
    targetData->ref();
    threadData.loadRelaxed()->deref();

    // synchronizes with loadAcquire e.g. in QCoreApplication::postEvent
    threadData.storeRelease(targetData);

    for (int i = 0; i < children.size(); ++i) {
        QObject *child = children.at(i);
        child->d_func()->setThreadData_helper(currentData, targetData, status);
    }
}""",
        """    }

    // guest: minimal affinity (skip ConnectionData walk + children recursion)
    if (!targetData)
        return;
    {
        QThreadData *oldData = threadData.loadRelaxed();
        if (oldData != targetData) {
            targetData->ref();
            if (oldData)
                oldData->deref();
            threadData.storeRelease(targetData);
        }
    }
}""",
    ),
]

changed = 0
for tag, old, new in patches:
    if new in text:
        print(f"[patch_setthreaddata_guest] {tag}: already applied")
    elif old in text:
        text = text.replace(old, new, 1)
        changed += 1
        print(f"[patch_setthreaddata_guest] {tag}: ok")
    else:
        print(f"[patch_setthreaddata_guest] {tag}: skip")

if changed:
    path.write_text(text)
print(f"[patch_setthreaddata_guest] done ({changed} updated)")
PY
