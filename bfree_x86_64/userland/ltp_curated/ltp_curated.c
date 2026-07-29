/* Curated LTP-style POSIX subset for the B-Free guest (F3 / SF-04).
 *
 * ABI-hole finder mix: FS basics + process/wait + pipe/fd + signals.
 * Output: "TPASS: <name>" / "TFAIL: <name>", then
 *   LTP_CURATED_RESULT: PASS|FAIL n=<N> fail=<F>
 * No ENOSYS / soft-fail escapes — real success only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass;
static int g_fail;

static void report(const char *name, int ok)
{
    printf("%s: %s\n", ok ? "TPASS" : "TFAIL", name);
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
    }
}

/* --- FS / basics (original 13-report set) --- */

static void open01(void)
{
    int fd = open("/tmp/ltp_open01", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = fd >= 0;

    if (fd >= 0) {
        close(fd);
        fd = open("/tmp/ltp_open01", O_RDONLY);
        ok = fd >= 0;
        if (fd >= 0) {
            close(fd);
        }
    }
    report("open01", ok);
}

static void write01_read01(void)
{
    char buf[16];
    int fd = open("/tmp/ltp_rw01", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = write(fd, "LTPDATA", 7) == 7;
        report("write01", ok);
        if (lseek(fd, 0, SEEK_SET) == 0 &&
            read(fd, buf, 7) == 7 && memcmp(buf, "LTPDATA", 7) == 0) {
            report("read01", 1);
        } else {
            report("read01", 0);
        }
        close(fd);
        return;
    }
    report("write01", 0);
    report("read01", 0);
}

static void lseek01(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    int ok = 0;

    if (fd >= 0) {
        ok = lseek(fd, 0, SEEK_END) == 7;
        close(fd);
    }
    report("lseek01", ok);
}

static void dup01(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    int ok = 0;

    if (fd >= 0) {
        int d = dup(fd);
        char c;

        ok = d >= 0 && read(d, &c, 1) == 1 && c == 'L';
        if (d >= 0) {
            close(d);
        }
        close(fd);
    }
    report("dup01", ok);
}

static void unlink01(void)
{
    int fd = open("/tmp/ltp_unlink01", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = unlink("/tmp/ltp_unlink01") == 0 &&
             open("/tmp/ltp_unlink01", O_RDONLY) < 0;
    }
    report("unlink01", ok);
}

static void pipe01(void)
{
    int p[2];
    char c = 0;
    int ok = pipe(p) == 0 &&
             write(p[1], "Z", 1) == 1 &&
             read(p[0], &c, 1) == 1 && c == 'Z';

    report("pipe01", ok);
    if (ok || p[0] >= 0) {
        close(p[0]);
        close(p[1]);
    }
}

static void getpid01(void)
{
    pid_t a = getpid();

    report("getpid01", a > 0 && a == getpid());
}

static void mkdir01_chdir01(void)
{
    int mk = mkdir("/tmp/ltp_dir01", 0755);
    int ok = (mk == 0 || errno == EEXIST);

    report("mkdir01", ok);
    report("chdir01", chdir("/tmp/ltp_dir01") == 0 && chdir("/") == 0);
}

static void stat01(void)
{
    struct stat st;
    int ok = stat("/tmp/ltp_rw01", &st) == 0 && st.st_size == 7;

    report("stat01", ok);
}

static void access01(void)
{
    int ok = access("/tmp/ltp_rw01", F_OK) == 0 &&
             access("/tmp/ltp_no_such", F_OK) != 0;

    report("access01", ok);
}

static void fcntl01(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    int ok = 0;

    if (fd >= 0) {
        ok = fcntl(fd, F_GETFD) >= 0;
        close(fd);
    }
    report("fcntl01", ok);
}

static void getuid01(void)
{
    report("getuid01", getuid() != (uid_t)-1 && geteuid() != (uid_t)-1);
}

static void time01(void)
{
    struct timespec a, b;
    int ok = clock_gettime(CLOCK_MONOTONIC, &a) == 0 &&
             clock_gettime(CLOCK_MONOTONIC, &b) == 0 &&
             (b.tv_sec > a.tv_sec ||
              (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));

    report("time01", ok);
}

/* --- A. process / wait --- */

/* Boot/init can leave unreaped zombies; wait(-1) prefers lowest pid. */
static void reap_zombies_nonblock(void)
{
    int st;

    while (waitpid(-1, &st, WNOHANG) > 0) {
    }
}

static void fork01(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("fork01", 0);
        return;
    }
    if (pid == 0) {
        _exit(0);
    }
    ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st);
    report("fork01", ok);
}

static void exit01(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("exit01", 0);
        return;
    }
    if (pid == 0) {
        _exit(7);
    }
    ok = waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 7;
    report("exit01", ok);
}

static void wait01(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;
    pid_t wr;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("wait01", 0);
        return;
    }
    if (pid == 0) {
        _exit(1);
    }
    wr = wait(&st);
    ok = wr == pid && WIFEXITED(st) && WEXITSTATUS(st) == 1;
    if (!ok && pid > 0) {
        (void)waitpid(pid, &st, 0);
    }
    report("wait01", ok);
}

static void waitpid01(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;
    pid_t wr;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("waitpid01", 0);
        return;
    }
    if (pid == 0) {
        _exit(2);
    }
    wr = waitpid(-1, &st, 0);
    ok = wr == pid && WIFEXITED(st) && WEXITSTATUS(st) == 2;
    if (!ok && pid > 0) {
        (void)waitpid(pid, &st, 0);
    }
    report("waitpid01", ok);
}

static void waitpid02(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;
    pid_t r;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("waitpid02", 0);
        return;
    }
    if (pid == 0) {
        _exit(3);
    }
    /* WNOHANG may see zombie immediately or 0 until scheduled; then block. */
    r = waitpid(pid, &st, WNOHANG);
    if (r == 0) {
        r = waitpid(pid, &st, 0);
    }
    ok = r == pid && WIFEXITED(st) && WEXITSTATUS(st) == 3;
    report("waitpid02", ok);
}

static void vfork01(void)
{
    pid_t pid;
    int st = -1;
    int ok = 0;

    reap_zombies_nonblock();
    pid = vfork();
    if (pid < 0) {
        report("vfork01", 0);
        return;
    }
    if (pid == 0) {
        _exit(0);
    }
    ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st);
    report("vfork01", ok);
}

static void getppid01(void)
{
    pid_t parent = getpid();
    pid_t pid;
    int st = -1;
    int ok = 0;

    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        report("getppid01", 0);
        return;
    }
    if (pid == 0) {
        _exit(getppid() == parent ? 0 : 1);
    }
    ok = waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
    report("getppid01", ok);
}

/* --- B. pipe / fd --- */

static void pipe02(void)
{
    int p[2];
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("pipe02", 0);
        return;
    }
    ok = write(p[1], "Q", 1) == 1 && read(p[0], &c, 1) == 1 && c == 'Q';
    close(p[1]);
    if (ok) {
        ok = read(p[0], &c, 1) == 0;
    }
    close(p[0]);
    report("pipe02", ok);
}

static void dup201(void)
{
    int fd = open("/tmp/ltp_dup2", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;
    char c;

    if (fd < 0) {
        report("dup201", 0);
        return;
    }
    if (write(fd, "D", 1) == 1 && lseek(fd, 0, SEEK_SET) == 0) {
        ok = dup2(fd, 20) == 20 && read(20, &c, 1) == 1 && c == 'D';
        close(20);
    }
    close(fd);
    unlink("/tmp/ltp_dup2");
    report("dup201", ok);
}

static void dup301(void)
{
    int fd = open("/tmp/ltp_dup3", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int d;
    int ok = 0;

    if (fd < 0) {
        report("dup301", 0);
        return;
    }
    d = dup3(fd, 21, O_CLOEXEC);
    ok = d == 21 && (fcntl(21, F_GETFD) & FD_CLOEXEC) != 0;
    if (d >= 0) {
        close(d);
    }
    close(fd);
    unlink("/tmp/ltp_dup3");
    report("dup301", ok);
}

static void fcntl02(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    int ok = 0;

    if (fd >= 0) {
        ok = fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
             (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
        close(fd);
    }
    report("fcntl02", ok);
}

static void fcntl03(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    int fl;
    int ok = 0;

    if (fd >= 0) {
        fl = fcntl(fd, F_GETFL);
        ok = fl >= 0 && (fl & O_ACCMODE) == O_RDONLY;
        close(fd);
    }
    report("fcntl03", ok);
}

static void close01(void)
{
    int fd = open("/tmp/ltp_close01", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = close(fd) == 0 && write(fd, "x", 1) < 0;
        unlink("/tmp/ltp_close01");
    }
    report("close01", ok);
}

static void writev01_readv01(void)
{
    int fd = open("/tmp/ltp_iov", O_RDWR | O_CREAT | O_TRUNC, 0644);
    char out[8] = "IOVDATA";
    char in[8];
    struct iovec ov, iv;
    int ok_w = 0;
    int ok_r = 0;

    ov.iov_base = out;
    ov.iov_len = 7;
    iv.iov_base = in;
    iv.iov_len = 7;
    if (fd >= 0) {
        ok_w = writev(fd, &ov, 1) == 7;
        report("writev01", ok_w);
        if (lseek(fd, 0, SEEK_SET) == 0) {
            ok_r = readv(fd, &iv, 1) == 7 && memcmp(in, "IOVDATA", 7) == 0;
        }
        report("readv01", ok_r);
        close(fd);
        unlink("/tmp/ltp_iov");
        return;
    }
    report("writev01", 0);
    report("readv01", 0);
}

static void pread01(void)
{
    int fd = open("/tmp/ltp_rw01", O_RDONLY);
    char buf[4];
    off_t pos;
    int ok = 0;

    if (fd >= 0) {
        pos = lseek(fd, 0, SEEK_SET);
        ok = pos == 0 &&
             pread(fd, buf, 3, 1) == 3 &&
             memcmp(buf, "TPD", 3) == 0 &&
             lseek(fd, 0, SEEK_CUR) == 0;
        close(fd);
    }
    report("pread01", ok);
}

/* --- C. signals --- */

static volatile sig_atomic_t g_got_usr1;

static void on_usr1(int sig)
{
    (void)sig;
    g_got_usr1 = 1;
}

/* Linux x86_64: kernel CATCH delivery needs sa_restorer → rt_sigreturn. */
static void sig_restorer(void)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(15) : "rcx", "r11", "memory");
    (void)ret;
}

static long raw_syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static int install_usr1_catch(void)
{
    /* Layout: handler, flags, restorer, mask (kernel k_sigaction). */
    long act[4];
    long i;

    for (i = 0; i < 4; ++i) {
        act[i] = 0;
    }
    act[0] = (long)(uintptr_t)on_usr1;
    act[2] = (long)(uintptr_t)sig_restorer;
    return (int)raw_syscall6(13, SIGUSR1, (long)(uintptr_t)act, 0, 8, 0, 0);
}

static void clear_usr1_catch(void)
{
    long act[4];
    long i;

    for (i = 0; i < 4; ++i) {
        act[i] = 0;
    }
    (void)raw_syscall6(13, SIGUSR1, (long)(uintptr_t)act, 0, 8, 0, 0);
}

static void kill01(void)
{
    report("kill01", kill(getpid(), 0) == 0);
}

static void kill02(void)
{
    int rc = kill(999999, 0);

    report("kill02", rc < 0 && (errno == ESRCH || errno == EPERM));
}

static void signal01(void)
{
    void (*prev)(int);

    g_got_usr1 = 0;
    prev = signal(SIGUSR1, on_usr1);
    report("signal01", prev != SIG_ERR);
    (void)signal(SIGUSR1, SIG_DFL);
}

static void sigaction01(void)
{
    g_got_usr1 = 0;
    if (install_usr1_catch() != 0) {
        report("sigaction01", 0);
        return;
    }
    if (kill(getpid(), SIGUSR1) != 0) {
        clear_usr1_catch();
        report("sigaction01", 0);
        return;
    }
    /* Enter kernel so pending CATCH can deliver. */
    (void)getpid();
    (void)getpid();
    clear_usr1_catch();
    report("sigaction01", g_got_usr1 != 0);
}

static void sigprocmask01(void)
{
    sigset_t set, old;
    int ok = 0;

    g_got_usr1 = 0;
    if (install_usr1_catch() != 0) {
        report("sigprocmask01", 0);
        return;
    }
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &set, &old) != 0) {
        clear_usr1_catch();
        report("sigprocmask01", 0);
        return;
    }
    (void)kill(getpid(), SIGUSR1);
    (void)getpid();
    /* Still blocked — handler must not have run. */
    if (g_got_usr1 != 0) {
        clear_usr1_catch();
        (void)sigprocmask(SIG_SETMASK, &old, NULL);
        report("sigprocmask01", 0);
        return;
    }
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) != 0) {
        clear_usr1_catch();
        report("sigprocmask01", 0);
        return;
    }
    (void)getpid();
    (void)getpid();
    ok = g_got_usr1 != 0;
    clear_usr1_catch();
    (void)sigprocmask(SIG_SETMASK, &old, NULL);
    report("sigprocmask01", ok);
}

static void alarm01(void)
{
    unsigned left;

    (void)alarm(0);
    left = alarm(5);
    report("alarm01", left == 0 && alarm(0) > 0);
}

static void pipe_sigpipe01(void)
{
    int p[2];
    void (*prev)(int);
    int ok = 0;

    if (pipe(p) != 0) {
        report("pipe_sigpipe01", 0);
        return;
    }
    prev = signal(SIGPIPE, SIG_IGN);
    close(p[0]);
    errno = 0;
    ok = write(p[1], "x", 1) < 0 && errno == EPIPE;
    close(p[1]);
    (void)signal(SIGPIPE, prev == SIG_ERR ? SIG_DFL : prev);
    report("pipe_sigpipe01", ok);
}

int main(void)
{
    /* FS basics */
    open01();
    write01_read01();
    lseek01();
    dup01();
    unlink01();
    pipe01();
    getpid01();
    mkdir01_chdir01();
    stat01();
    access01();
    fcntl01();
    getuid01();
    time01();

    /* A. process / wait */
    fork01();
    exit01();
    wait01();
    waitpid01();
    waitpid02();
    vfork01();
    getppid01();

    /* B. pipe / fd */
    pipe02();
    dup201();
    dup301();
    fcntl02();
    fcntl03();
    close01();
    writev01_readv01();
    pread01();

    /* C. signals */
    kill01();
    kill02();
    signal01();
    sigaction01();
    sigprocmask01();
    alarm01();
    pipe_sigpipe01();

    printf("LTP_CURATED_RESULT: %s n=%d fail=%d\n",
           g_fail == 0 ? "PASS" : "FAIL", g_pass + g_fail, g_fail);
    fflush(stdout);
    /* _exit avoids musl atexit against the shared guest fd table. */
    _exit(g_fail == 0 ? 0 : 1);
}
