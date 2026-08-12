#include "guest_bfree_shell_process.h"

#include <stddef.h>

static long guest_sh_sys1(long n, long a)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}

static long guest_sh_sys2(long n, long a, long b)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}

static long guest_sh_sys3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}

enum {
    GSH_O_RDWR = 2,
    GSH_O_NONBLOCK = 2048,
    GSH_F_GETFL = 3,
    GSH_F_SETFL = 4,
    GSH_TIOCGPTN = 0x80045430UL
};

int guest_pty_shell_start(GuestPtyShell *sh)
{
    static char bb_path[] = "/busybox.elf";
    static char sh_argv0[] = "sh";
    static char *sh_argv[2];
    static char env_user[] = "USER=root";
    static char env_home[] = "HOME=/root";
    static char env_path[] = "PATH=/bin:/usr/bin:.";
    static char env_shell[] = "SHELL=/bin/sh";
    static char env_term[] = "TERM=linux";
    static char env_ps1[] = "PS1=root@bfree:# ";
    static char *envp[8];
    static char pts_path[] = "/dev/pts/0";
    unsigned int ptn;
    long mfd;
    long sfd;
    long pid;
    long fl;

    if (!sh)
        return -1;
    sh->master_fd = -1;
    sh->shell_pid = -1;
    sh->active = 0;

    mfd = guest_sh_sys3(2 /* open */, (long)"/dev/ptmx", GSH_O_RDWR, 0);
    if (mfd < 0)
        return -1;

    ptn = 0;
    if (guest_sh_sys3(16 /* ioctl */, mfd, (long)GSH_TIOCGPTN, (long)&ptn) < 0) {
        (void)guest_sh_sys1(3, mfd);
        return -1;
    }
    if (ptn > 9) {
        (void)guest_sh_sys1(3, mfd);
        return -1;
    }
    pts_path[9] = (char)('0' + (int)ptn);
    pts_path[10] = '\0';

    sfd = guest_sh_sys3(2, (long)pts_path, GSH_O_RDWR, 0);
    if (sfd < 0) {
        (void)guest_sh_sys1(3, mfd);
        return -1;
    }

    fl = guest_sh_sys3(72 /* fcntl */, mfd, GSH_F_GETFL, 0);
    if (fl >= 0)
        (void)guest_sh_sys3(72, mfd, GSH_F_SETFL, fl | GSH_O_NONBLOCK);

    sh_argv[0] = sh_argv0;
    sh_argv[1] = nullptr;
    envp[0] = env_user;
    envp[1] = env_home;
    envp[2] = env_path;
    envp[3] = env_shell;
    envp[4] = env_term;
    envp[5] = env_ps1;
    envp[6] = nullptr;

    pid = guest_sh_sys1(58 /* vfork */, 0);
    if (pid < 0) {
        (void)guest_sh_sys1(3, sfd);
        (void)guest_sh_sys1(3, mfd);
        return -1;
    }
    if (pid == 0) {
        (void)guest_sh_sys2(33 /* dup2 */, sfd, 0);
        (void)guest_sh_sys2(33, sfd, 1);
        (void)guest_sh_sys2(33, sfd, 2);
        if (sfd > 2)
            (void)guest_sh_sys1(3, sfd);
        (void)guest_sh_sys3(59 /* execve */, (long)bb_path, (long)sh_argv, (long)envp);
        guest_sh_sys1(60 /* exit */, 127);
        for (;;)
            __asm__ volatile("pause" ::: "memory");
    }

    (void)guest_sh_sys1(3, sfd);
    sh->master_fd = mfd;
    sh->shell_pid = pid;
    sh->active = 1;
    return 0;
}

void guest_pty_shell_stop(GuestPtyShell *sh)
{
    if (!sh || !sh->active)
        return;
    if (sh->shell_pid > 0) {
        (void)guest_sh_sys2(62 /* kill */, sh->shell_pid, 15 /* SIGTERM */);
        (void)guest_sh_sys3(61 /* waitpid */, sh->shell_pid, 0, 0);
    }
    if (sh->master_fd >= 0)
        (void)guest_sh_sys1(3, sh->master_fd);
    sh->master_fd = -1;
    sh->shell_pid = -1;
    sh->active = 0;
}

int guest_pty_shell_poll(GuestPtyShell *sh, char *buf, int bufsz)
{
    long n;

    if (!sh || !sh->active || sh->master_fd < 0 || !buf || bufsz <= 0)
        return 0;
    n = guest_sh_sys3(0 /* read */, sh->master_fd, (long)buf, (long)(bufsz - 1));
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    return (int)n;
}

int guest_pty_shell_write(GuestPtyShell *sh, const char *data, int len)
{
    long n;
    int i;

    if (!sh || !sh->active || sh->master_fd < 0 || !data || len <= 0)
        return -1;
    for (i = 0; i < len; ++i) {
        n = guest_sh_sys3(1 /* write */, sh->master_fd, (long)(data + i), 1);
        if (n <= 0)
            return -1;
    }
    return 0;
}

int guest_pty_shell_write_byte(GuestPtyShell *sh, char c)
{
    return guest_pty_shell_write(sh, &c, 1);
}

GuestBFreeShellProcess::GuestBFreeShellProcess(QObject *parent) : QObject(parent) {}

bool GuestBFreeShellProcess::start()
{
    if (m_shell.active)
        return true;
    return guest_pty_shell_start(&m_shell) == 0;
}

void GuestBFreeShellProcess::stop()
{
    guest_pty_shell_stop(&m_shell);
}
