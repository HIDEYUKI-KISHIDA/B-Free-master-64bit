#pragma once

#include <QObject>

/* PTY-backed persistent BusyBox ash session for FB terminal. */
struct GuestPtyShell {
    long master_fd;
    long shell_pid;
    int active;
};

#ifdef __cplusplus
extern "C" {
#endif

int guest_pty_shell_start(GuestPtyShell *sh);
void guest_pty_shell_stop(GuestPtyShell *sh);
int guest_pty_shell_poll(GuestPtyShell *sh, char *buf, int bufsz);
int guest_pty_shell_write(GuestPtyShell *sh, const char *data, int len);
int guest_pty_shell_write_byte(GuestPtyShell *sh, char c);

#ifdef __cplusplus
}
#endif

class GuestBFreeShellProcess : public QObject
{
    Q_OBJECT

public:
    explicit GuestBFreeShellProcess(QObject *parent = nullptr);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool active() const { return m_shell.active != 0; }

private:
    GuestPtyShell m_shell{};
};
