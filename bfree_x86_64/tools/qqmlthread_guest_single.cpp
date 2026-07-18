// B-Free guest: single-thread QQmlTypeLoader (replaces threaded qqmlthread.cpp).
// Matches /root/src/qt6 qtdeclarative qqmlthread_p.h (template API, no threadObject).

#include "qqmlthread_p.h"

#include <private/qfieldlist_p.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qcoreevent.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qmutex.h>
#include <QtCore/qthread.h>

QT_BEGIN_NAMESPACE

class QQmlThreadPrivate : public QObject
{
public:
    using MessageList = QFieldList<QQmlThread::Message, &QQmlThread::Message::next>;

    explicit QQmlThreadPrivate(QQmlThread *q) : qq(q)
    {
        setObjectName(QStringLiteral("QQmlThread"));
    }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::User) {
            processing = true;
            while (!messages.isEmpty()) {
                QQmlThread::Message *message = messages.takeFirst();
                message->call(qq);
                delete message;
            }
            processing = false;
        }
        return QObject::event(e);
    }

    QQmlThread *qq = nullptr;
    MessageList messages;
    bool processing = false;
    bool shutdown = false;
    QMutex dummyMutex;
};

QQmlThread::QQmlThread() : d(new QQmlThreadPrivate(this)) {}
QQmlThread::~QQmlThread() { delete d; }

void QQmlThread::startup() {}
void QQmlThread::shutdown()
{
    d->shutdown = true;
    discardMessages();
}

bool QQmlThread::isShutdown() const
{
    return d->shutdown;
}

QMutex &QQmlThread::mutex()
{
    return d->dummyMutex;
}

void QQmlThread::lock() {}
void QQmlThread::unlock() {}
void QQmlThread::wakeOne() {}
void QQmlThread::wait() {}

QThread *QQmlThread::thread() const
{
    return d->thread();
}

bool QQmlThread::isThisThread() const
{
    return d->thread()->isCurrentThread();
}

void QQmlThread::internalCallMethodInThread(Message *message)
{
    internalCallMethodInMain(message);
}

void QQmlThread::internalCallMethodInMain(Message *message)
{
    message->call(this);
    delete message;
}

void QQmlThread::internalPostMethodToThread(Message *message)
{
    internalPostMethodToMain(message);
}

void QQmlThread::internalPostMethodToMain(Message *message)
{
    const bool wasEmpty = d->messages.isEmpty();
    d->messages.append(message);
    if (wasEmpty && !d->processing)
        QCoreApplication::postEvent(d, new QEvent(QEvent::User));
}

void QQmlThread::waitForNextMessage()
{
    for (int round = 0; round < 16 && QCoreApplication::instance(); ++round)
        QCoreApplication::processEvents(QEventLoop::AllEvents);
}

void QQmlThread::discardMessages()
{
    while (!d->messages.isEmpty())
        delete d->messages.takeFirst();
}

QT_END_NAMESPACE
