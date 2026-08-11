#pragma once

#include <QObject>

/* QML hook for future interactive ash session (PTY). FB terminal uses sh -c oneshot. */
class GuestBFreeShellProcess : public QObject
{
    Q_OBJECT

public:
    explicit GuestBFreeShellProcess(QObject *parent = nullptr) : QObject(parent) {}
};
