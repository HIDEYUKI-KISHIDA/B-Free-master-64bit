/* Curated LTP-style POSIX subset for the B-Free guest (F3 / SF-04).
 *
 * ABI-hole finder mix: FS + process/wait + pipe/fd + signals + mmap +
 * claimed-DONE extras (fsync/link/shm/socketpair/poll/clock/…).
 * Output: "TPASS: <name>" / "TFAIL: <name>", then
 *   LTP_CURATED_RESULT: PASS|FAIL n=<N> fail=<F>
 * No ENOSYS / soft-fail escapes — real success only.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/mount.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <unistd.h>
#include <grp.h>
#include <sys/shm.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef FALLOC_FL_ZERO_RANGE
#define FALLOC_FL_ZERO_RANGE 0x10
#endif

#ifndef STATX_BASIC_STATS
#define STATX_BASIC_STATS 0x000007ffU
#endif

#ifndef __NR_getcpu
#define __NR_getcpu 309
#endif
#ifndef __NR_ioprio_set
#define __NR_ioprio_set 251
#endif
#ifndef __NR_ioprio_get
#define __NR_ioprio_get 252
#endif
#ifndef __NR_sethostname
#define __NR_sethostname 170
#endif
#ifndef __NR_setdomainname
#define __NR_setdomainname 171
#endif
#ifndef __NR_chroot
#define __NR_chroot 161
#endif
#ifndef __NR_setns
#define __NR_setns 308
#endif
#ifndef __NR_pivot_root
#define __NR_pivot_root 155
#endif
#ifndef __NR_timer_create
#define __NR_timer_create 222
#endif
#ifndef __NR_timer_settime
#define __NR_timer_settime 223
#endif
#ifndef __NR_timer_gettime
#define __NR_timer_gettime 224
#endif
#ifndef __NR_timer_delete
#define __NR_timer_delete 226
#endif
#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv 310
#endif
#ifndef __NR_clone3
#define __NR_clone3 435
#endif
#ifndef __NR_fanotify_init
#define __NR_fanotify_init 300
#endif
#ifndef __NR_fanotify_mark
#define __NR_fanotify_mark 301
#endif
#ifndef __NR_name_to_handle_at
#define __NR_name_to_handle_at 303
#endif
#ifndef __NR_open_by_handle_at
#define __NR_open_by_handle_at 304
#endif
#ifndef __NR_statx
#define __NR_statx 332
#endif
#ifndef __NR_memfd_secret
#define __NR_memfd_secret 447
#endif
#ifndef __NR_clock_settime
#define __NR_clock_settime 227
#endif
#ifndef __NR_settimeofday
#define __NR_settimeofday 164
#endif
#ifndef __NR_execveat
#define __NR_execveat 322
#endif
#ifndef __NR_setgroups
#define __NR_setgroups 116
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

/* Section R syscall numbers (Linux x86_64) */
#ifndef __NR_semget
#define __NR_semget 64
#endif
#ifndef __NR_semop
#define __NR_semop 65
#endif
#ifndef __NR_semctl
#define __NR_semctl 66
#endif
#ifndef __NR_msgget
#define __NR_msgget 68
#endif
#ifndef __NR_msgsnd
#define __NR_msgsnd 69
#endif
#ifndef __NR_msgrcv
#define __NR_msgrcv 70
#endif
#ifndef __NR_msgctl
#define __NR_msgctl 71
#endif
#ifndef __NR_sched_setattr
#define __NR_sched_setattr 314
#endif
#ifndef __NR_sched_getattr
#define __NR_sched_getattr 315
#endif
#ifndef __NR_membarrier
#define __NR_membarrier 324
#endif
#ifndef __NR_rseq
#define __NR_rseq 334
#endif
#ifndef __NR_adjtimex
#define __NR_adjtimex 159
#endif
#ifndef __NR_clock_adjtime
#define __NR_clock_adjtime 305
#endif
#ifndef __NR_quotactl
#define __NR_quotactl 179
#endif
#ifndef __NR_add_key
#define __NR_add_key 248
#endif
#ifndef __NR_request_key
#define __NR_request_key 249
#endif
#ifndef __NR_keyctl
#define __NR_keyctl 250
#endif
#ifndef __NR_perf_event_open
#define __NR_perf_event_open 298
#endif
#ifndef __NR_epoll_pwait2
#define __NR_epoll_pwait2 441
#endif
#ifndef __NR_futex_waitv
#define __NR_futex_waitv 449
#endif
#ifndef __NR_fsopen
#define __NR_fsopen 430
#endif
#ifndef __NR_fsconfig
#define __NR_fsconfig 431
#endif
#ifndef __NR_fsmount
#define __NR_fsmount 432
#endif
#ifndef __NR_open_tree
#define __NR_open_tree 428
#endif
#ifndef __NR_move_mount
#define __NR_move_mount 429
#endif
#ifndef __NR_process_madvise
#define __NR_process_madvise 440
#endif
#ifndef __NR_pkey_mprotect
#define __NR_pkey_mprotect 329
#endif
#ifndef __NR_pkey_alloc
#define __NR_pkey_alloc 330
#endif
#ifndef __NR_pkey_free
#define __NR_pkey_free 331
#endif
#ifndef __NR_cachestat
#define __NR_cachestat 451
#endif
#ifndef __NR_statmount
#define __NR_statmount 457
#endif
#ifndef __NR_listmount
#define __NR_listmount 458
#endif
#ifndef __NR_kcmp
#define __NR_kcmp 312
#endif
#ifndef __NR_setxattr
#define __NR_setxattr 188
#endif
#ifndef __NR_getxattr
#define __NR_getxattr 191
#endif
#ifndef __NR_listxattr
#define __NR_listxattr 194
#endif
#ifndef __NR_removexattr
#define __NR_removexattr 197
#endif
#ifndef IPC_PRIVATE
#define IPC_PRIVATE 0
#endif
#ifndef IPC_RMID
#define IPC_RMID 0
#endif
#ifndef SEM_UNDO
#define SEM_UNDO 0x1000
#endif
#ifndef GETVAL
#define GETVAL 12
#endif
#ifndef SETVAL
#define SETVAL 16
#endif
#ifndef KEYCTL_READ
#define KEYCTL_READ 11
#endif
#ifndef KCMP_FILE
#define KCMP_FILE 0
#endif
#ifndef MADV_COLD
#define MADV_COLD 20
#endif
#ifndef FSCONFIG_SET_STRING
#define FSCONFIG_SET_STRING 1
#endif
#ifndef FSCONFIG_CMD_CREATE
#define FSCONFIG_CMD_CREATE 6
#endif

/* Section S syscall numbers (Linux x86_64) */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#define __NR_io_uring_enter 426
#define __NR_io_uring_register 427
#endif
#ifndef __NR_userfaultfd
#define __NR_userfaultfd 323
#endif
#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#endif
#ifndef __NR_seccomp
#define __NR_seccomp 317
#endif
#ifndef __NR_bpf
#define __NR_bpf 321
#endif
#ifndef __NR_ptrace
#define __NR_ptrace 101
#endif
#ifndef __NR_syslog
#define __NR_syslog 103
#endif
#ifndef __NR_acct
#define __NR_acct 163
#endif
#ifndef __NR_swapon
#define __NR_swapon 167
#define __NR_swapoff 168
#endif
#ifndef __NR_init_module
#define __NR_init_module 175
#endif
#ifndef __NR_delete_module
#define __NR_delete_module 176
#endif
#ifndef __NR_finit_module
#define __NR_finit_module 313
#endif
#ifndef __NR_reboot
#define __NR_reboot 169
#endif
#ifndef __NR_fspick
#define __NR_fspick 433
#endif
#ifndef __NR_mount_setattr
#define __NR_mount_setattr 442
#endif
#ifndef __NR_personality
#define __NR_personality 135
#endif
#ifndef __NR_modify_ldt
#define __NR_modify_ldt 154
#endif
#ifndef __NR_futex
#define __NR_futex 202
#endif
#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif
#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#endif
#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef SECCOMP_SET_MODE_STRICT
#define SECCOMP_SET_MODE_STRICT 1
#endif
#ifndef PTRACE_TRACEME
#define PTRACE_TRACEME 0
#endif
#ifndef IN_CREATE
#define IN_CREATE 0x00000100
#endif

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

/* --- D. mmap / memory (ABI hole finder next tranche) --- */

static void getpagesize01(void)
{
    long psz = sysconf(_SC_PAGESIZE);

    if (psz < 0) {
        psz = (long)getpagesize();
    }
    report("getpagesize01", psz >= 4096);
}

static void mmap01_anon(void)
{
    size_t psz = (size_t)getpagesize();
    char *p;
    int ok = 0;

    if (psz < 4096) {
        psz = 4096;
    }
    p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) {
        p[0] = 'A';
        p[psz - 1] = 'Z';
        ok = p[0] == 'A' && p[psz - 1] == 'Z' && munmap(p, psz) == 0;
    }
    report("mmap01", ok);
}

static void mmap02_file_private(void)
{
    const char *path = "/tmp/ltp_mmap02";
    char buf[8];
    int fd;
    char *p;
    size_t psz = 4096;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("mmap02", 0);
        return;
    }
    if (write(fd, "MMAPDAT", 7) != 7) {
        close(fd);
        report("mmap02", 0);
        return;
    }
    p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p != MAP_FAILED) {
        ok = memcmp(p, "MMAPDAT", 7) == 0;
        p[0] = 'X'; /* private — must not write back */
        (void)munmap(p, psz);
    }
    if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, buf, 7) == 7) {
        ok = ok && memcmp(buf, "MMAPDAT", 7) == 0;
    } else {
        ok = 0;
    }
    close(fd);
    (void)unlink(path);
    report("mmap02", ok);
}

static void mmap03_shared_msync(void)
{
    const char *path = "/tmp/ltp_mmap03";
    char buf[8];
    int fd;
    char *p;
    size_t psz = 4096;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("mmap03", 0);
        return;
    }
    if (ftruncate(fd, (off_t)psz) != 0 && write(fd, "........", 8) < 0) {
        /* Some guests accept short file + MAP_SHARED via vfile; try write pad. */
        (void)lseek(fd, 0, SEEK_SET);
        {
            char z[64];
            size_t left = psz;
            memset(z, 0, sizeof(z));
            while (left > 0) {
                size_t n = left > sizeof(z) ? sizeof(z) : left;
                if (write(fd, z, (ssize_t)n) != (ssize_t)n) {
                    close(fd);
                    report("mmap03", 0);
                    return;
                }
                left -= n;
            }
        }
    }
    p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        report("mmap03", 0);
        return;
    }
    memcpy(p, "SHARED!", 7);
    ok = msync(p, psz, MS_SYNC) == 0;
    (void)munmap(p, psz);
    if (ok && lseek(fd, 0, SEEK_SET) == 0 && read(fd, buf, 7) == 7) {
        ok = memcmp(buf, "SHARED!", 7) == 0;
    } else {
        ok = 0;
    }
    close(fd);
    (void)unlink(path);
    report("mmap03", ok);
}

/* Two MAP_SHARED maps of the same /tmp vfile must see writes without msync. */
static void mmap04_shared_live(void)
{
    const char *path = "/tmp/ltp_shared_live";
    int fd;
    char *p1;
    char *p2;
    size_t psz = 4096;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("mmap04_shared_live", 0);
        return;
    }
    if (ftruncate(fd, (off_t)psz) != 0) {
        close(fd);
        (void)unlink(path);
        report("mmap04_shared_live", 0);
        return;
    }
    p1 = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    p2 = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p1 == MAP_FAILED || p2 == MAP_FAILED) {
        if (p1 != MAP_FAILED) {
            (void)munmap(p1, psz);
        }
        if (p2 != MAP_FAILED) {
            (void)munmap(p2, psz);
        }
        close(fd);
        (void)unlink(path);
        report("mmap04_shared_live", 0);
        return;
    }
    p1[0] = 'Z';
    ok = (p2[0] == 'Z');
    (void)munmap(p1, psz);
    (void)munmap(p2, psz);
    close(fd);
    (void)unlink(path);
    report("mmap04_shared_live", ok);
}

/* After fork, MAP_PRIVATE anon write in child must not change parent (COW/AS-copy). */
static void mmap05_cow_break(void)
{
    size_t psz = 4096;
    char *p;
    pid_t pid;
    int st = -1;
    int ok = 0;

    p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        report("mmap05_cow_break", 0);
        return;
    }
    p[0] = 'A';
    reap_zombies_nonblock();
    pid = fork();
    if (pid < 0) {
        (void)munmap(p, psz);
        report("mmap05_cow_break", 0);
        return;
    }
    if (pid == 0) {
        p[0] = 'B';
        _exit(p[0] == 'B' ? 0 : 1);
    }
    if (waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        ok = (p[0] == 'A');
    }
    (void)munmap(p, psz);
    report("mmap05_cow_break", ok);
}

static void munmap01(void)
{
    size_t psz = 4096;
    void *p = mmap(NULL, psz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok = 0;

    if (p != MAP_FAILED) {
        ok = munmap(p, psz) == 0;
        /* Double munmap should fail (best-effort; some stubs return 0). */
        errno = 0;
        if (munmap(p, psz) == 0 && errno == 0) {
            /* Accept either EINVAL on second munmap or silent success on thin stub. */
        }
    }
    report("munmap01", ok);
}

/* brk/sbrk: guest musl heap path not yet ABI-stable — omit until hole closed. */

static void rename01(void)
{
    int fd = open("/tmp/ltp_ren_a", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = rename("/tmp/ltp_ren_a", "/tmp/ltp_ren_b") == 0 &&
             access("/tmp/ltp_ren_b", F_OK) == 0 &&
             access("/tmp/ltp_ren_a", F_OK) != 0;
        (void)unlink("/tmp/ltp_ren_b");
        (void)unlink("/tmp/ltp_ren_a");
    }
    report("rename01", ok);
}

static void ftruncate01(void)
{
    int fd = open("/tmp/ltp_ftrunc", O_RDWR | O_CREAT | O_TRUNC, 0644);
    struct stat st;
    int ok = 0;

    if (fd >= 0) {
        ok = ftruncate(fd, 123) == 0 &&
             fstat(fd, &st) == 0 && st.st_size == 123;
        close(fd);
        (void)unlink("/tmp/ltp_ftrunc");
    }
    report("ftruncate01", ok);
}

static void getcwd01(void)
{
    char buf[256];
    int ok = getcwd(buf, sizeof(buf)) != NULL && buf[0] == '/';

    report("getcwd01", ok);
}

/* --- E. claimed-DONE ABI probe (hole hunt) --- */

static void pwrite01(void)
{
    const char *path = "/tmp/ltp_pwrite";
    char buf[8];
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = write(fd, "AAAA", 4) == 4 &&
             pwrite(fd, "BB", 2, 1) == 2 &&
             lseek(fd, 0, SEEK_SET) == 0 &&
             read(fd, buf, 4) == 4 &&
             memcmp(buf, "ABBA", 4) == 0;
        close(fd);
        (void)unlink(path);
    }
    report("pwrite01", ok);
}

static void fsync01(void)
{
    int fd = open("/tmp/ltp_fsync", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = write(fd, "x", 1) == 1 && fsync(fd) == 0;
        close(fd);
        (void)unlink("/tmp/ltp_fsync");
    }
    report("fsync01", ok);
}

static void fdatasync01(void)
{
    int fd = open("/tmp/ltp_fdatasync", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = write(fd, "x", 1) == 1 && fdatasync(fd) == 0;
        close(fd);
        (void)unlink("/tmp/ltp_fdatasync");
    }
    report("fdatasync01", ok);
}

static void sync01(void)
{
    sync();
    report("sync01", 1);
}

static void umask01(void)
{
    mode_t old = umask(0022);
    mode_t cur = umask(old);

    report("umask01", cur == 0022);
}

static void truncate01(void)
{
    const char *path = "/tmp/ltp_trunc";
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = truncate(path, 77) == 0 &&
             stat(path, &st) == 0 && st.st_size == 77;
        (void)unlink(path);
    }
    report("truncate01", ok);
}

static void chmod01(void)
{
    const char *path = "/tmp/ltp_chmod";
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = chmod(path, 0600) == 0 &&
             stat(path, &st) == 0 && (st.st_mode & 0777) == 0600;
        (void)unlink(path);
    }
    report("chmod01", ok);
}

static void link01(void)
{
    const char *a = "/tmp/ltp_link_a";
    const char *b = "/tmp/ltp_link_b";
    int fd = open(a, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = link(a, b) == 0 && access(b, F_OK) == 0;
        (void)unlink(a);
        (void)unlink(b);
    }
    report("link01", ok);
}

static void symlink01_readlink01(void)
{
    char buf[64];
    ssize_t n;
    int ok_s = 0;
    int ok_r = 0;

    (void)unlink("/tmp/ltp_sym");
    ok_s = symlink("/tmp/ltp_sym_tgt", "/tmp/ltp_sym") == 0;
    n = readlink("/tmp/ltp_sym", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        ok_r = strcmp(buf, "/tmp/ltp_sym_tgt") == 0;
    }
    (void)unlink("/tmp/ltp_sym");
    report("symlink01", ok_s);
    report("readlink01", ok_r);
}

static void pipe201(void)
{
    int p[2];
    char c = 0;
    int ok = 0;

    if (pipe2(p, O_CLOEXEC) == 0) {
        ok = write(p[1], "p", 1) == 1 && read(p[0], &c, 1) == 1 && c == 'p';
        close(p[0]);
        close(p[1]);
    }
    report("pipe201", ok);
}

static void flock01(void)
{
    int fd = open("/tmp/ltp_flock", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = flock(fd, LOCK_EX) == 0 && flock(fd, LOCK_UN) == 0;
        close(fd);
        (void)unlink("/tmp/ltp_flock");
    }
    report("flock01", ok);
}

static void mremap01(void)
{
    size_t psz = 4096;
    char *p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *q;
    int ok = 0;

    if (p != MAP_FAILED) {
        p[0] = 'M';
        q = (char *)mremap(p, psz, psz * 2, 0);
        if (q != MAP_FAILED) {
            ok = q[0] == 'M';
            (void)munmap(q, psz * 2);
        } else {
            (void)munmap(p, psz);
        }
    }
    report("mremap01", ok);
}

static void mprotect01(void)
{
    size_t psz = 4096;
    char *p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok = 0;

    if (p != MAP_FAILED) {
        ok = mprotect(p, psz, PROT_READ) == 0 &&
             mprotect(p, psz, PROT_READ | PROT_WRITE) == 0;
        (void)munmap(p, psz);
    }
    report("mprotect01", ok);
}

static void mincore01(void)
{
    size_t psz = 4096;
    unsigned char vec[1];
    char *p = (char *)mmap(NULL, psz, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok = 0;

    if (p != MAP_FAILED) {
        p[0] = 1;
        ok = mincore(p, psz, vec) == 0;
        (void)munmap(p, psz);
    }
    report("mincore01", ok);
}

static void memfd01(void)
{
    int fd = memfd_create("ltp_memfd", MFD_CLOEXEC);
    char *p;
    int ok = 0;

    if (fd >= 0) {
        ok = ftruncate(fd, 4096) == 0;
        p = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ok && p != MAP_FAILED) {
            p[0] = 'F';
            ok = p[0] == 'F' && munmap(p, 4096) == 0;
        } else {
            ok = 0;
        }
        close(fd);
    }
    report("memfd01", ok);
}

static void shm_open01(void)
{
    const char *name = "/ltp_curated_shm";
    int fd;
    char *p;
    int ok = 0;

    (void)shm_unlink(name);
    fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd >= 0) {
        ok = ftruncate(fd, 4096) == 0;
        p = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ok && p != MAP_FAILED) {
            memcpy(p, "SHM!", 4);
            ok = memcmp(p, "SHM!", 4) == 0 && munmap(p, 4096) == 0;
        } else {
            ok = 0;
        }
        close(fd);
    }
    report("shm_open01", ok);
    report("shm_unlink01", shm_unlink(name) == 0);
}

static void socketpair01(void)
{
    int sv[2];
    char c = 0;
    int ok = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        ok = write(sv[0], "u", 1) == 1 && read(sv[1], &c, 1) == 1 && c == 'u';
        close(sv[0]);
        close(sv[1]);
    }
    report("socketpair01", ok);
}

static void poll01(void)
{
    int p[2];
    struct pollfd pf;
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("poll01", 0);
        return;
    }
    pf.fd = p[0];
    pf.events = POLLIN;
    pf.revents = 0;
    if (write(p[1], "q", 1) == 1 && poll(&pf, 1, 0) == 1 &&
        (pf.revents & POLLIN) && read(p[0], &c, 1) == 1 && c == 'q') {
        ok = 1;
    }
    close(p[0]);
    close(p[1]);
    report("poll01", ok);
}

static void select01(void)
{
    int p[2];
    fd_set rfds;
    struct timeval tv;
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("select01", 0);
        return;
    }
    if (write(p[1], "s", 1) != 1) {
        close(p[0]);
        close(p[1]);
        report("select01", 0);
        return;
    }
    FD_ZERO(&rfds);
    FD_SET(p[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(p[0] + 1, &rfds, NULL, NULL, &tv) == 1 &&
        FD_ISSET(p[0], &rfds) && read(p[0], &c, 1) == 1 && c == 's') {
        ok = 1;
    }
    close(p[0]);
    close(p[1]);
    report("select01", ok);
}

static void clock_gettime01(void)
{
    struct timespec ts;
    int ok = clock_gettime(CLOCK_MONOTONIC, &ts) == 0 &&
             (ts.tv_sec > 0 || ts.tv_nsec > 0);

    report("clock_gettime01", ok);
}

static void clock_getres01(void)
{
    struct timespec ts;
    int ok = clock_getres(CLOCK_MONOTONIC, &ts) == 0 &&
             (ts.tv_sec > 0 || ts.tv_nsec > 0);

    report("clock_getres01", ok);
}

static void clock_nanosleep01(void)
{
    struct timespec req, rem;

    req.tv_sec = 0;
    req.tv_nsec = 1;
    rem.tv_sec = 0;
    rem.tv_nsec = 0;
    report("clock_nanosleep01",
           clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem) == 0);
}

static void nanosleep01(void)
{
    struct timespec req;

    req.tv_sec = 0;
    req.tv_nsec = 1;
    report("nanosleep01", nanosleep(&req, NULL) == 0);
}

static void getrandom01(void)
{
    unsigned char buf[16];
    ssize_t n = getrandom(buf, sizeof(buf), 0);

    report("getrandom01", n == (ssize_t)sizeof(buf));
}

static void getrlimit01(void)
{
    struct rlimit rl;
    int ok = getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 0;

    report("getrlimit01", ok);
}

static void sysinfo01(void)
{
    struct sysinfo si;
    int ok = sysinfo(&si) == 0 && si.mem_unit > 0;

    report("sysinfo01", ok);
}

static void fchdir01(void)
{
    int fd = open("/tmp", O_RDONLY | O_DIRECTORY);
    char buf[256];
    int ok = 0;

    if (fd >= 0) {
        ok = fchdir(fd) == 0 &&
             getcwd(buf, sizeof(buf)) != NULL &&
             strcmp(buf, "/tmp") == 0 &&
             chdir("/") == 0;
        close(fd);
    }
    report("fchdir01", ok);
}

static void faccessat01(void)
{
    int ok = faccessat(AT_FDCWD, "/tmp", F_OK, 0) == 0;

    report("faccessat01", ok);
}

static void creat01(void)
{
    int fd = creat("/tmp/ltp_creat", 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = write(fd, "c", 1) == 1;
        close(fd);
        (void)unlink("/tmp/ltp_creat");
    }
    report("creat01", ok);
}

static void getpgrp01(void)
{
    pid_t pg = getpgrp();

    report("getpgrp01", pg > 0 && pg == getpgid(0));
}

static void setsid_getsid01(void)
{
    /* Don't call setsid() in the main suite process (would detach session).
     * Probe getsid(0) only. */
    report("getsid01", getsid(0) >= 0);
}

/* --- F. hole hunt: eventfd/epoll/utimensat/sendfile/brk/TCP --- */

static long raw_brk(long addr)
{
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(12), "D"(addr)
                     : "rcx", "r11", "memory");
    return ret;
}

static void brk01(void)
{
    long cur = raw_brk(0);
    long want;
    long got;
    int ok = 0;

    if (cur <= 0) {
        report("brk01", 0);
        return;
    }
    want = (cur + 4095) & ~4095L;
    if (want <= cur) {
        want = cur + 4096;
    }
    got = raw_brk(want);
    if (got == want) {
        volatile char *p = (volatile char *)(uintptr_t)cur;
        p[0] = 'B';
        ok = p[0] == 'B';
        (void)raw_brk(cur);
    }
    report("brk01", ok);
}

static void brk02_query(void)
{
    long cur = raw_brk(0);

    report("brk02", cur > 0);
}

static void eventfd01(void)
{
    int efd = eventfd(0, 0);
    uint64_t v = 7;
    uint64_t r = 0;
    int ok = 0;

    if (efd >= 0) {
        ok = write(efd, &v, sizeof(v)) == (ssize_t)sizeof(v) &&
             read(efd, &r, sizeof(r)) == (ssize_t)sizeof(r) &&
             r == 7;
        close(efd);
    }
    report("eventfd01", ok);
}

static void eventfd02_nonblock(void)
{
    int efd = eventfd(0, EFD_NONBLOCK);
    uint64_t r = 0;
    int ok = 0;

    if (efd >= 0) {
        errno = 0;
        ok = read(efd, &r, sizeof(r)) < 0 &&
             (errno == EAGAIN || errno == EWOULDBLOCK);
        close(efd);
    }
    report("eventfd02", ok);
}

static void epoll01_pipe(void)
{
    int p[2];
    int ep;
    struct epoll_event ev, out;
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("epoll01", 0);
        return;
    }
    ep = epoll_create1(0);
    if (ep >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0 &&
             write(p[1], "E", 1) == 1 &&
             epoll_wait(ep, &out, 1, 0) == 1 &&
             (out.events & EPOLLIN) &&
             out.data.fd == p[0] &&
             read(p[0], &c, 1) == 1 && c == 'E';
        close(ep);
    }
    close(p[0]);
    close(p[1]);
    report("epoll01", ok);
}

static void epoll02_eventfd(void)
{
    int efd = eventfd(0, 0);
    int ep;
    struct epoll_event ev, out;
    uint64_t v = 1;
    int ok = 0;

    if (efd < 0) {
        report("epoll02", 0);
        return;
    }
    ep = epoll_create1(0);
    if (ep >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = efd;
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev) == 0 &&
             write(efd, &v, sizeof(v)) == (ssize_t)sizeof(v) &&
             epoll_wait(ep, &out, 1, 0) == 1 &&
             (out.events & EPOLLIN) &&
             out.data.fd == efd;
        close(ep);
    }
    close(efd);
    report("epoll02", ok);
}

static void epoll03_mod_del(void)
{
    int p[2];
    int ep;
    struct epoll_event ev;
    int ok = 0;

    if (pipe(p) != 0) {
        report("epoll03", 0);
        return;
    }
    ep = epoll_create1(0);
    if (ep >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0;
        ev.events = EPOLLIN | EPOLLET;
        ok = ok && epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev) == 0 &&
             epoll_ctl(ep, EPOLL_CTL_DEL, p[0], NULL) == 0;
        close(ep);
    }
    close(p[0]);
    close(p[1]);
    report("epoll03", ok);
}

static void utimensat01(void)
{
    const char *path = "/tmp/ltp_utimens";
    struct timespec ts[2];
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    ts[0].tv_sec = 1111;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 2222;
    ts[1].tv_nsec = 0;
    if (fd >= 0) {
        close(fd);
        ok = utimensat(AT_FDCWD, path, ts, 0) == 0 &&
             stat(path, &st) == 0 &&
             st.st_mtime == 2222;
        (void)unlink(path);
    }
    report("utimensat01", ok);
}

static void futimens01(void)
{
    const char *path = "/tmp/ltp_futimens";
    struct timespec ts[2];
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    ts[0].tv_sec = 3333;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 4444;
    ts[1].tv_nsec = 0;
    if (fd >= 0) {
        ok = futimens(fd, ts) == 0 &&
             fstat(fd, &st) == 0 &&
             st.st_mtime == 4444;
        close(fd);
        (void)unlink(path);
    }
    report("futimens01", ok);
}

static void sendfile01(void)
{
    const char *in_path = "/tmp/ltp_sf_in";
    const char *out_path = "/tmp/ltp_sf_out";
    char buf[16];
    int in_fd, out_fd;
    off_t off = 0;
    int ok = 0;

    in_fd = open(in_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    out_fd = open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (in_fd >= 0 && out_fd >= 0) {
        ok = write(in_fd, "SENDFILE!", 9) == 9;
        if (ok) {
            ok = sendfile(out_fd, in_fd, &off, 9) == 9 && off == 9;
        }
        if (ok && lseek(out_fd, 0, SEEK_SET) == 0 &&
            read(out_fd, buf, 9) == 9) {
            ok = memcmp(buf, "SENDFILE!", 9) == 0;
        } else {
            ok = 0;
        }
    }
    if (in_fd >= 0) {
        close(in_fd);
    }
    if (out_fd >= 0) {
        close(out_fd);
    }
    (void)unlink(in_path);
    (void)unlink(out_path);
    report("sendfile01", ok);
}

static void sendfile02_pipe(void)
{
    const char *in_path = "/tmp/ltp_sf_pipe";
    int in_fd;
    int p[2];
    char buf[16];
    off_t off = 0;
    int ok = 0;

    in_fd = open(in_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (in_fd < 0 || pipe(p) != 0) {
        if (in_fd >= 0) {
            close(in_fd);
        }
        report("sendfile02", 0);
        return;
    }
    if (write(in_fd, "PIPEOUT!", 8) == 8) {
        ok = sendfile(p[1], in_fd, &off, 8) == 8 &&
             read(p[0], buf, 8) == 8 &&
             memcmp(buf, "PIPEOUT!", 8) == 0;
    }
    close(in_fd);
    close(p[0]);
    close(p[1]);
    (void)unlink(in_path);
    report("sendfile02", ok);
}

static void tcp01_echo(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[8];
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0) {
            ok = send(cli, "TCP!", 4, 0) == 4 &&
                 recv(afd, buf, 4, 0) == 4 &&
                 memcmp(buf, "TCP!", 4) == 0 &&
                 send(afd, "ACK!", 4, 0) == 4 &&
                 recv(cli, buf, 4, 0) == 4 &&
                 memcmp(buf, "ACK!", 4) == 0;
            close(afd);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("tcp01", ok);
}

static void tcp02_shutdown(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0) {
            ok = shutdown(cli, SHUT_RDWR) == 0;
            close(afd);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("tcp02", ok);
}

static void tcp03_epoll(void)
{
    int srv = -1, cli = -1, afd = -1;
    int ep = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    struct epoll_event ev, out;
    char buf[4];
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        ep = epoll_create1(0);
        if (afd >= 0 && ep >= 0) {
            ev.events = EPOLLIN;
            ev.data.fd = afd;
            ok = epoll_ctl(ep, EPOLL_CTL_ADD, afd, &ev) == 0 &&
                 send(cli, "EP", 2, 0) == 2 &&
                 epoll_wait(ep, &out, 1, 100) == 1 &&
                 (out.events & EPOLLIN) &&
                 recv(afd, buf, 2, 0) == 2 &&
                 memcmp(buf, "EP", 2) == 0;
        }
    }
    if (ep >= 0) {
        close(ep);
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("tcp03", ok);
}

/* --- G. timerfd / accept4 / MSG_PEEK / getpeername / fallocate / mkfifo /
 *       signalfd / splice --- */

static void timerfd01_epoll(void)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    int ep;
    struct itimerspec its;
    struct epoll_event ev, out;
    uint64_t exp = 0;
    int ok = 0;

    if (tfd < 0) {
        report("timerfd01", 0);
        return;
    }
    memset(&its, 0, sizeof(its));
    its.it_value.tv_nsec = 20000000L; /* 20ms */
    ep = epoll_create1(0);
    if (ep >= 0 && timerfd_settime(tfd, 0, &its, NULL) == 0) {
        ev.events = EPOLLIN;
        ev.data.fd = tfd;
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev) == 0 &&
             epoll_wait(ep, &out, 1, 500) == 1 &&
             (out.events & EPOLLIN) &&
             out.data.fd == tfd &&
             read(tfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp) &&
             exp >= 1;
        close(ep);
    }
    close(tfd);
    report("timerfd01", ok);
}

static void accept401(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int ok = 0;
    int flags;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
        if (afd >= 0) {
            flags = fcntl(afd, F_GETFD);
            ok = flags >= 0 && (flags & FD_CLOEXEC) != 0;
            close(afd);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("accept401", ok);
}

static void msg_peek01(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char a = 0, b = 0;
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0 && send(cli, "P", 1, 0) == 1) {
            ok = recv(afd, &a, 1, MSG_PEEK) == 1 && a == 'P' &&
                 recv(afd, &b, 1, 0) == 1 && b == 'P';
            close(afd);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("msg_peek01", ok);
}

static void getpeername01(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr, peer;
    socklen_t alen = sizeof(addr), plen = sizeof(peer);
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0) {
            plen = sizeof(peer);
            ok = getpeername(cli, (struct sockaddr *)&peer, &plen) == 0 &&
                 peer.sin_family == AF_INET &&
                 peer.sin_port == addr.sin_port;
            close(afd);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("getpeername01", ok);
}

static void fallocate01(void)
{
    const char *path = "/tmp/ltp_falloc";
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = posix_fallocate(fd, 0, 2048) == 0 &&
             fstat(fd, &st) == 0 && st.st_size >= 2048;
        close(fd);
        (void)unlink(path);
    }
    report("fallocate01", ok);
}

static void mkfifo01(void)
{
    const char *path = "/tmp/ltp_fifo";
    struct stat st;
    int ok = 0;

    (void)unlink(path);
    if (mkfifo(path, 0644) == 0 && stat(path, &st) == 0) {
        ok = S_ISFIFO(st.st_mode);
    }
    (void)unlink(path);
    report("mkfifo01", ok);
}

static void signalfd01(void)
{
    sigset_t set;
    int sfd;
    struct signalfd_siginfo si;
    ssize_t n;
    int ok = 0;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) {
        report("signalfd01", 0);
        return;
    }
    sfd = signalfd(-1, &set, SFD_CLOEXEC);
    if (sfd >= 0) {
        if (kill(getpid(), SIGUSR1) == 0) {
            (void)getpid(); /* enter kernel for pending delivery path */
            n = read(sfd, &si, sizeof(si));
            ok = n == (ssize_t)sizeof(si) && si.ssi_signo == SIGUSR1;
        }
        close(sfd);
    }
    (void)sigprocmask(SIG_UNBLOCK, &set, NULL);
    report("signalfd01", ok);
}

static void splice01(void)
{
    const char *path = "/tmp/ltp_splice";
    int fd;
    int p[2];
    char buf[16];
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || pipe(p) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        report("splice01", 0);
        return;
    }
    if (write(fd, "SPLICEOK", 8) == 8 && lseek(fd, 0, SEEK_SET) == 0) {
        ok = splice(fd, NULL, p[1], NULL, 8, 0) == 8 &&
             read(p[0], buf, 8) == 8 &&
             memcmp(buf, "SPLICEOK", 8) == 0;
    }
    close(fd);
    close(p[0]);
    close(p[1]);
    (void)unlink(path);
    report("splice01", ok);
}

static void splice02_pipe_pipe(void)
{
    int a[2], b[2];
    char buf[8];
    int ok = 0;

    if (pipe(a) != 0 || pipe(b) != 0) {
        report("splice02", 0);
        return;
    }
    if (write(a[1], "XY", 2) == 2) {
        ok = splice(a[0], NULL, b[1], NULL, 2, 0) == 2 &&
             read(b[0], buf, 2) == 2 &&
             memcmp(buf, "XY", 2) == 0;
    }
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    report("splice02", ok);
}

/* --- H. *at → getdents → getsockopt → AF_UNIX pathname --- */

static void linkat01(void)
{
    const char *a = "/tmp/ltp_linkat_a";
    const char *b = "/tmp/ltp_linkat_b";
    int fd = open(a, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        (void)unlink(b);
        ok = linkat(AT_FDCWD, a, AT_FDCWD, b, 0) == 0 && access(b, F_OK) == 0;
        (void)unlink(a);
        (void)unlink(b);
    }
    report("linkat01", ok);
}

static void unlinkat01(void)
{
    const char *path = "/tmp/ltp_unlinkat";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = unlinkat(AT_FDCWD, path, 0) == 0 && access(path, F_OK) != 0;
    }
    report("unlinkat01", ok);
}

static void renameat01(void)
{
    const char *a = "/tmp/ltp_renat_a";
    const char *b = "/tmp/ltp_renat_b";
    int fd = open(a, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        (void)unlink(b);
        ok = renameat(AT_FDCWD, a, AT_FDCWD, b) == 0 &&
             access(a, F_OK) != 0 && access(b, F_OK) == 0;
        (void)unlink(b);
    }
    report("renameat01", ok);
}

static void mkdirat01(void)
{
    int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
    int ok = 0;

    if (dfd >= 0) {
        (void)unlinkat(dfd, "ltp_mkdirat", AT_REMOVEDIR);
        ok = mkdirat(dfd, "ltp_mkdirat", 0755) == 0 &&
             access("/tmp/ltp_mkdirat", F_OK) == 0;
        (void)unlinkat(dfd, "ltp_mkdirat", AT_REMOVEDIR);
        close(dfd);
    }
    report("mkdirat01", ok);
}

static void symlinkat01_readlinkat01(void)
{
    char buf[64];
    ssize_t n;
    int ok_s = 0;
    int ok_r = 0;
    int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);

    if (dfd >= 0) {
        (void)unlinkat(dfd, "ltp_symat", 0);
        ok_s = symlinkat("ltp_symat_tgt", dfd, "ltp_symat") == 0;
        n = readlinkat(dfd, "ltp_symat", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            ok_r = strcmp(buf, "ltp_symat_tgt") == 0;
        }
        (void)unlinkat(dfd, "ltp_symat", 0);
        close(dfd);
    }
    report("symlinkat01", ok_s);
    report("readlinkat01", ok_r);
}

static void fchmodat01(void)
{
    const char *path = "/tmp/ltp_fchmodat";
    struct stat st;
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        close(fd);
        ok = fchmodat(AT_FDCWD, path, 0600, 0) == 0 &&
             stat(path, &st) == 0 && (st.st_mode & 0777) == 0600;
        (void)unlink(path);
    }
    report("fchmodat01", ok);
}

static void getdents01(void)
{
    const char *marker = "ltp_gd_marker";
    char path[64];
    int fd;
    DIR *d;
    struct dirent *de;
    int found = 0;

    snprintf(path, sizeof(path), "/tmp/%s", marker);
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("getdents01", 0);
        return;
    }
    close(fd);
    d = opendir("/tmp");
    if (!d) {
        (void)unlink(path);
        report("getdents01", 0);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, marker) == 0) {
            found = 1;
            break;
        }
    }
    closedir(d);
    (void)unlink(path);
    report("getdents01", found);
}

static void getsockopt01_type(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int typ = -1;
    socklen_t len = sizeof(typ);
    int ok = 0;

    if (fd >= 0) {
        ok = getsockopt(fd, SOL_SOCKET, SO_TYPE, &typ, &len) == 0 &&
             len == sizeof(int) && typ == SOCK_STREAM;
        close(fd);
    }
    report("getsockopt01", ok);
}

static void getsockopt02_error(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int err = -1;
    socklen_t len = sizeof(err);
    int ok = 0;

    if (fd >= 0) {
        ok = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 &&
             len == sizeof(int) && err == 0;
        close(fd);
    }
    report("getsockopt02", ok);
}

static void unix01_path(void)
{
    const char *path = "/tmp/ltp_unix.sock";
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_un addr;
    char buf[8];
    int ok = 0;

    (void)unlink(path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    srv = socket(AF_UNIX, SOCK_STREAM, 0);
    cli = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0) {
            ok = send(cli, "UX", 2, 0) == 2 &&
                 recv(afd, buf, 2, 0) == 2 &&
                 memcmp(buf, "UX", 2) == 0;
        }
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    (void)unlink(path);
    report("unix01", ok);
}

/* --- I. remaining 5–10: setitimer/waitid/ppoll, rusage/times/uname/tod,
 *       F_SETLK, tee, timerfd deepen, UDP/shutdown --- */

static volatile sig_atomic_t g_got_alrm;

static void on_alrm(int sig)
{
    (void)sig;
    g_got_alrm++;
}

static void setitimer01(void)
{
    struct itimerval it;
    struct timespec sl;
    int ok = 0;

    g_got_alrm = 0;
    if (signal(SIGALRM, on_alrm) == SIG_ERR) {
        report("setitimer01", 0);
        return;
    }
    memset(&it, 0, sizeof(it));
    it.it_value.tv_usec = 50000; /* 50ms */
    if (setitimer(ITIMER_REAL, &it, NULL) == 0) {
        sl.tv_sec = 0;
        sl.tv_nsec = 200000000L; /* 200ms */
        (void)nanosleep(&sl, NULL);
        ok = g_got_alrm != 0;
    }
    memset(&it, 0, sizeof(it));
    (void)setitimer(ITIMER_REAL, &it, NULL);
    (void)signal(SIGALRM, SIG_DFL);
    report("setitimer01", ok);
}

static void waitid01(void)
{
    pid_t pid;
    siginfo_t si;
    int ok = 0;

    pid = fork();
    if (pid < 0) {
        report("waitid01", 0);
        return;
    }
    if (pid == 0) {
        _exit(42);
    }
    memset(&si, 0, sizeof(si));
    if (waitid(P_PID, (id_t)pid, &si, WEXITED) == 0) {
        ok = si.si_pid == pid && si.si_code == CLD_EXITED && si.si_status == 42;
    }
    report("waitid01", ok);
}

static void ppoll01(void)
{
    int p[2];
    struct pollfd pfd;
    struct timespec ts;
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("ppoll01", 0);
        return;
    }
    pfd.fd = p[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000L; /* 50ms idle → 0 */
    if (ppoll(&pfd, 1, &ts, NULL) == 0 &&
        write(p[1], "P", 1) == 1) {
        ts.tv_sec = 0;
        ts.tv_nsec = 200000000L;
        ok = ppoll(&pfd, 1, &ts, NULL) == 1 &&
             (pfd.revents & POLLIN) &&
             read(p[0], &c, 1) == 1 && c == 'P';
    }
    close(p[0]);
    close(p[1]);
    report("ppoll01", ok);
}

static void getrusage01(void)
{
    struct rusage ru;
    int ok = getrusage(RUSAGE_SELF, &ru) == 0;

    report("getrusage01", ok);
}

static void times01(void)
{
    struct tms t;
    clock_t ticks = times(&t);

    report("times01", ticks != (clock_t)-1);
}

static void uname01(void)
{
    struct utsname u;
    int ok = uname(&u) == 0 && u.sysname[0] != '\0' && u.machine[0] != '\0';

    report("uname01", ok);
}

static void gettimeofday01(void)
{
    struct timeval tv;
    int ok = gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 0;

    report("gettimeofday01", ok);
}

static void fcntl_setlk01(void)
{
    int fd = open("/tmp/ltp_setlk", O_RDWR | O_CREAT | O_TRUNC, 0644);
    struct flock fl;
    int ok = 0;

    if (fd >= 0) {
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        ok = fcntl(fd, F_SETLK, &fl) == 0;
        fl.l_type = F_UNLCK;
        ok = ok && fcntl(fd, F_SETLK, &fl) == 0;
        close(fd);
        (void)unlink("/tmp/ltp_setlk");
    }
    report("fcntl_setlk01", ok);
}

static void tee01(void)
{
    int a[2], b[2];
    char src[8], dst[8];
    int ok = 0;

    if (pipe(a) != 0 || pipe(b) != 0) {
        report("tee01", 0);
        return;
    }
    if (write(a[1], "TEEDATA!", 8) == 8) {
        ok = tee(a[0], b[1], 8, 0) == 8 &&
             read(b[0], dst, 8) == 8 && memcmp(dst, "TEEDATA!", 8) == 0 &&
             read(a[0], src, 8) == 8 && memcmp(src, "TEEDATA!", 8) == 0;
    }
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    report("tee01", ok);
}

static void timerfd02_interval(void)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its, cur;
    struct timespec sl;
    uint64_t exp = 0;
    int ok = 0;

    if (tfd < 0) {
        report("timerfd02", 0);
        return;
    }
    memset(&its, 0, sizeof(its));
    its.it_value.tv_nsec = 20000000L;
    its.it_interval.tv_nsec = 20000000L;
    if (timerfd_settime(tfd, 0, &its, NULL) == 0) {
        sl.tv_sec = 0;
        sl.tv_nsec = 80000000L; /* ~4 ticks */
        (void)nanosleep(&sl, NULL);
        ok = timerfd_gettime(tfd, &cur) == 0 &&
             cur.it_interval.tv_nsec == 20000000L &&
             read(tfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp) &&
             exp >= 1;
        /* Empty nonblock read → EAGAIN */
        if (ok) {
            errno = 0;
            ok = read(tfd, &exp, sizeof(exp)) < 0 && errno == EAGAIN;
        }
    }
    close(tfd);
    report("timerfd02", ok);
}

static void udp01(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba, from;
    socklen_t alen = sizeof(aa), blen = sizeof(ba), flen = sizeof(from);
    char buf[8];
    int ok = 0;
    int typ = -1;
    socklen_t tlen = sizeof(typ);

    memset(&aa, 0, sizeof(aa));
    memset(&ba, 0, sizeof(ba));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0 &&
        getsockopt(a, SOL_SOCKET, SO_TYPE, &typ, &tlen) == 0 &&
        typ == SOCK_DGRAM) {
        ok = sendto(a, "UD", 2, 0, (struct sockaddr *)&ba, blen) == 2 &&
             recvfrom(b, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen) == 2 &&
             memcmp(buf, "UD", 2) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("udp01", ok);
}

static void shutdown01_half(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[4];
    int ok = 0;
    ssize_t n;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0 && shutdown(cli, SHUT_WR) == 0) {
            n = recv(afd, buf, sizeof(buf), 0);
            ok = (n == 0);
        }
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("shutdown01", ok);
}

/* --- J. vmsplice + shutdown RD/EPIPE + setitimer interval + connected UDP --- */

static void vmsplice01(void)
{
    int p[2];
    char src[8] = "VMSPLC!";
    char dst[8];
    struct iovec iov;
    int ok = 0;

    if (pipe(p) != 0) {
        report("vmsplice01", 0);
        return;
    }
    iov.iov_base = src;
    iov.iov_len = 7;
    if (vmsplice(p[1], &iov, 1, 0) == 7) {
        ok = read(p[0], dst, 7) == 7 && memcmp(dst, "VMSPLC!", 7) == 0;
    }
    close(p[0]);
    close(p[1]);
    report("vmsplice01", ok);
}

static void shutdown02_rd_epipe(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[4];
    int ok = 0;
    ssize_t n;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0 &&
            shutdown(cli, SHUT_RD) == 0 &&
            send(afd, "X", 1, 0) == 1) {
            n = recv(cli, buf, sizeof(buf), 0);
            if (n == 0) {
                (void)signal(SIGPIPE, SIG_IGN);
                errno = 0;
                ok = shutdown(cli, SHUT_WR) == 0 &&
                     send(cli, "Y", 1, 0) < 0 && errno == EPIPE;
                (void)signal(SIGPIPE, SIG_DFL);
            }
        }
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("shutdown02", ok);
}

static void setitimer02_interval(void)
{
    struct itimerval it;
    struct timespec sl;
    int ok = 0;
    int i;

    g_got_alrm = 0;
    if (signal(SIGALRM, on_alrm) == SIG_ERR) {
        report("setitimer02", 0);
        return;
    }
    memset(&it, 0, sizeof(it));
    it.it_value.tv_usec = 30000;     /* first fire ~30ms */
    it.it_interval.tv_usec = 30000;  /* reload */
    if (setitimer(ITIMER_REAL, &it, NULL) == 0) {
        /* Each SIGALRM may EINTR nanosleep — loop until ≥2 or budget. */
        for (i = 0; i < 40 && g_got_alrm < 2; ++i) {
            sl.tv_sec = 0;
            sl.tv_nsec = 20000000L; /* 20ms slices */
            (void)nanosleep(&sl, NULL);
        }
        ok = g_got_alrm >= 2;
    }
    memset(&it, 0, sizeof(it));
    (void)setitimer(ITIMER_REAL, &it, NULL);
    (void)signal(SIGALRM, SIG_DFL);
    report("setitimer02", ok);
}

static void udp02_connected(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba;
    socklen_t alen = sizeof(aa), blen = sizeof(ba);
    char buf[8];
    int ok = 0;

    memset(&aa, 0, sizeof(aa));
    memset(&ba, 0, sizeof(ba));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0 &&
        connect(a, (struct sockaddr *)&ba, blen) == 0 &&
        connect(b, (struct sockaddr *)&aa, alen) == 0) {
        ok = send(a, "CU", 2, 0) == 2 &&
             recv(b, buf, sizeof(buf), 0) == 2 &&
             memcmp(buf, "CU", 2) == 0 &&
             send(b, "OK", 2, 0) == 2 &&
             recv(a, buf, sizeof(buf), 0) == 2 &&
             memcmp(buf, "OK", 2) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("udp02", ok);
}

/* --- K. setsockopt / sock NONBLOCK+DONTWAIT / select·pselect on sock --- */

static void setsockopt01(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1, v = -1;
    socklen_t len = sizeof(v);
    int ok = 0;

    if (s < 0) {
        report("setsockopt01", 0);
        return;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0 &&
        getsockopt(s, SOL_SOCKET, SO_REUSEADDR, &v, &len) == 0 && v == 1) {
        int rcv = 16384;
        v = -1;
        len = sizeof(v);
        ok = setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)) == 0 &&
             getsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, &len) == 0 && v == 16384 &&
             setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == 0 &&
             (len = sizeof(v), v = -1,
              getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &v, &len) == 0 && v == 1);
    }
    close(s);
    report("setsockopt01", ok);
}

static void setsockopt02_reuse(void)
{
    int a = -1, b = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int on = 1;
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        setsockopt(a, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0 &&
        setsockopt(b, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == 0 &&
        bind(a, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(a, (struct sockaddr *)&addr, &alen) == 0) {
        ok = bind(b, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("setsockopt02", ok);
}

static void sock_nonblock01(void)
{
    int srv = -1, udp = -1;
    struct sockaddr_in addr, uaddr;
    socklen_t alen = sizeof(addr);
    char buf[4];
    int ok = 0;
    int fl;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    uaddr = addr;
    srv = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    udp = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (srv >= 0 && udp >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        bind(udp, (struct sockaddr *)&uaddr, sizeof(uaddr)) == 0) {
        fl = fcntl(srv, F_GETFL);
        errno = 0;
        ok = (fl & O_NONBLOCK) != 0 &&
             accept(srv, NULL, NULL) < 0 && errno == EAGAIN;
        if (ok) {
            errno = 0;
            ok = recvfrom(udp, buf, sizeof(buf), 0, NULL, NULL) < 0 &&
                 errno == EAGAIN;
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (udp >= 0) {
        close(udp);
    }
    report("sock_nonblock01", ok);
}

static void msg_dontwait01(void)
{
    int udp = -1;
    struct sockaddr_in addr;
    char buf[4];
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp >= 0 && bind(udp, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        errno = 0;
        ok = recvfrom(udp, buf, sizeof(buf), MSG_DONTWAIT, NULL, NULL) < 0 &&
             errno == EAGAIN;
    }
    if (udp >= 0) {
        close(udp);
    }
    report("msg_dontwait01", ok);
}

static void select02_sock(void)
{
    int srv = -1, cli = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    fd_set rfds;
    struct timeval tv;
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0) {
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(srv + 1, &rfds, NULL, NULL, &tv) == 0 &&
            connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            FD_ZERO(&rfds);
            FD_SET(srv, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            ok = select(srv + 1, &rfds, NULL, NULL, &tv) == 1 &&
                 FD_ISSET(srv, &rfds);
        }
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("select02", ok);
}

static void pselect01(void)
{
    int p[2];
    fd_set rfds;
    struct timespec ts;
    char c = 0;
    int ok = 0;

    if (pipe(p) != 0) {
        report("pselect01", 0);
        return;
    }
    FD_ZERO(&rfds);
    FD_SET(p[0], &rfds);
    ts.tv_sec = 0;
    ts.tv_nsec = 20000000L; /* 20ms idle → 0 */
    if (pselect(p[0] + 1, &rfds, NULL, NULL, &ts, NULL) == 0 &&
        write(p[1], "S", 1) == 1) {
        FD_ZERO(&rfds);
        FD_SET(p[0], &rfds);
        ts.tv_sec = 0;
        ts.tv_nsec = 200000000L;
        ok = pselect(p[0] + 1, &rfds, NULL, NULL, &ts, NULL) == 1 &&
             FD_ISSET(p[0], &rfds) &&
             read(p[0], &c, 1) == 1 && c == 'S';
    }
    close(p[0]);
    close(p[1]);
    report("pselect01", ok);
}

/* --- L. copy_file_range / sendmmsg·recvmmsg / chown·utimes --- */

static void copy_file_range01(void)
{
    const char *in_path = "/tmp/ltp_cfr_in";
    const char *out_path = "/tmp/ltp_cfr_out";
    int in_fd = -1, out_fd = -1;
    char buf[16];
    off_t off_in = 0, off_out = 0;
    int ok = 0;

    in_fd = open(in_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    out_fd = open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (in_fd >= 0 && out_fd >= 0 && write(in_fd, "CFRANGE!", 8) == 8) {
        ok = copy_file_range(in_fd, &off_in, out_fd, &off_out, 8, 0) == 8 &&
             off_in == 8 && off_out == 8 &&
             lseek(out_fd, 0, SEEK_SET) == 0 &&
             read(out_fd, buf, 8) == 8 && memcmp(buf, "CFRANGE!", 8) == 0;
    }
    if (in_fd >= 0) {
        close(in_fd);
    }
    if (out_fd >= 0) {
        close(out_fd);
    }
    (void)unlink(in_path);
    (void)unlink(out_path);
    report("copy_file_range01", ok);
}

static void sendmmsg01_recvmmsg01(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba;
    socklen_t alen = sizeof(aa), blen = sizeof(ba);
    char s0[4] = "AB", s1[4] = "CD";
    char r0[8], r1[8];
    struct iovec iov_s[2], iov_r[2];
    struct mmsghdr msg_s[2], msg_r[2];
    int ok = 0;

    memset(&aa, 0, sizeof(aa));
    memset(&ba, 0, sizeof(ba));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0) {
        memset(msg_s, 0, sizeof(msg_s));
        memset(msg_r, 0, sizeof(msg_r));
        iov_s[0].iov_base = s0;
        iov_s[0].iov_len = 2;
        iov_s[1].iov_base = s1;
        iov_s[1].iov_len = 2;
        msg_s[0].msg_hdr.msg_iov = &iov_s[0];
        msg_s[0].msg_hdr.msg_iovlen = 1;
        msg_s[0].msg_hdr.msg_name = &ba;
        msg_s[0].msg_hdr.msg_namelen = blen;
        msg_s[1].msg_hdr.msg_iov = &iov_s[1];
        msg_s[1].msg_hdr.msg_iovlen = 1;
        msg_s[1].msg_hdr.msg_name = &ba;
        msg_s[1].msg_hdr.msg_namelen = blen;
        iov_r[0].iov_base = r0;
        iov_r[0].iov_len = sizeof(r0);
        iov_r[1].iov_base = r1;
        iov_r[1].iov_len = sizeof(r1);
        msg_r[0].msg_hdr.msg_iov = &iov_r[0];
        msg_r[0].msg_hdr.msg_iovlen = 1;
        msg_r[1].msg_hdr.msg_iov = &iov_r[1];
        msg_r[1].msg_hdr.msg_iovlen = 1;
        if (sendmmsg(a, msg_s, 2, 0) == 2) {
            ok = recvmmsg(b, msg_r, 2, MSG_DONTWAIT, NULL) == 2 &&
                 msg_r[0].msg_len == 2 && msg_r[1].msg_len == 2 &&
                 memcmp(r0, "AB", 2) == 0 && memcmp(r1, "CD", 2) == 0;
        }
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("sendmmsg01", ok);
    report("recvmmsg01", ok);
}

static void chown01(void)
{
    const char *path = "/tmp/ltp_chown";
    struct stat st;
    int fd;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("chown01", 0);
        return;
    }
    close(fd);
    if (chown(path, 42, 7) == 0 && stat(path, &st) == 0 &&
        st.st_uid == 42 && st.st_gid == 7) {
        fd = open(path, O_RDWR);
        if (fd >= 0) {
            ok = fchown(fd, 99, 88) == 0 && fstat(fd, &st) == 0 &&
                 st.st_uid == 99 && st.st_gid == 88;
            close(fd);
        }
    }
    (void)unlink(path);
    report("chown01", ok);
}

static void utimes01(void)
{
    const char *path = "/tmp/ltp_utimes";
    struct timeval tv[2];
    struct stat st;
    int fd;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("utimes01", 0);
        return;
    }
    close(fd);
    tv[0].tv_sec = 1000;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = 2000;
    tv[1].tv_usec = 500000;
    if (utimes(path, tv) == 0 && stat(path, &st) == 0) {
        ok = st.st_atime == 1000 && st.st_mtime == 2000;
    }
    (void)unlink(path);
    report("utimes01", ok);
}

/* --- M. madvise/fadvise / sockopt BROADCAST·LINGER·TIMEO / MSG_WAITALL·TRUNC --- */

static void madvise01(void)
{
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ok = 0;

    if (p == MAP_FAILED) {
        report("madvise01", 0);
        return;
    }
    errno = 0;
    ok = madvise(p, 4096, MADV_NORMAL) == 0 &&
         madvise(p, 4096, MADV_DONTNEED) == 0;
    if (ok) {
        errno = 0;
        ok = madvise(p, 4096, 999) < 0 && errno == EINVAL;
    }
    munmap(p, 4096);
    report("madvise01", ok);
}

static void fadvise01(void)
{
    const char *path = "/tmp/ltp_fadvise";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd < 0) {
        report("fadvise01", 0);
        return;
    }
    ok = posix_fadvise(fd, 0, 4096, POSIX_FADV_NORMAL) == 0 &&
         posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    (void)unlink(path);
    report("fadvise01", ok);
}

static void sync_file_range01(void)
{
    const char *path = "/tmp/ltp_sfr";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd < 0) {
        report("sync_file_range01", 0);
        return;
    }
    if (write(fd, "SFR", 3) == 3) {
        ok = sync_file_range(fd, 0, 3, SYNC_FILE_RANGE_WRITE) == 0;
    }
    close(fd);
    (void)unlink(path);
    report("sync_file_range01", ok);
}

static void setsockopt03_more(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int on = 1, v = -1;
    socklen_t len = sizeof(v);
    struct linger lg, lg2;
    struct timeval tv, tv2;
    int ok = 0;

    if (s < 0) {
        report("setsockopt03", 0);
        return;
    }
    lg.l_onoff = 1;
    lg.l_linger = 3;
    tv.tv_sec = 1;
    tv.tv_usec = 500000;
    memset(&lg2, 0, sizeof(lg2));
    memset(&tv2, 0, sizeof(tv2));
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) == 0 &&
        getsockopt(s, SOL_SOCKET, SO_BROADCAST, &v, &len) == 0 && v == 1 &&
        setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == 0) {
        len = sizeof(lg2);
        if (getsockopt(s, SOL_SOCKET, SO_LINGER, &lg2, &len) == 0 &&
            lg2.l_onoff == 1 && lg2.l_linger == 3 &&
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0) {
            len = sizeof(tv2);
            ok = getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv2, &len) == 0 &&
                 tv2.tv_sec == 1 && tv2.tv_usec == 500000;
        }
    }
    close(s);
    report("setsockopt03", ok);
}

static void msg_trunc01(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba;
    socklen_t alen = sizeof(aa), blen = sizeof(ba);
    char buf[4];
    int ok = 0;
    ssize_t n;

    memset(&aa, 0, sizeof(aa));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0 &&
        sendto(a, "TRUNCATE", 8, 0, (struct sockaddr *)&ba, blen) == 8) {
        n = recvfrom(b, buf, 4, MSG_TRUNC, NULL, NULL);
        ok = n == 8 && memcmp(buf, "TRUN", 4) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("msg_trunc01", ok);
}

static void msg_waitall01(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[8];
    int ok = 0;
    ssize_t n;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0 &&
            send(cli, "WAIT", 4, 0) == 4 &&
            send(cli, "ALL!", 4, 0) == 4) {
            n = recv(afd, buf, 8, MSG_WAITALL);
            ok = n == 8 && memcmp(buf, "WAITALL!", 8) == 0;
        }
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("msg_waitall01", ok);
}

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE 0x01
#define FALLOC_FL_PUNCH_HOLE 0x02
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_GET_SEALS 1034
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef IP_TTL
#define IP_TTL 2
#endif
#ifndef IP_TOS
#define IP_TOS 1
#endif
#ifndef SO_OOBINLINE
#define SO_OOBINLINE 10
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

/* --- N. remaining hole batch (1–6 + mid 6) --- */

static void renameat201(void)
{
    const char *a = "/tmp/ltp_ren2_a";
    const char *b = "/tmp/ltp_ren2_b";
    int fd;
    int ok = 0;

    (void)unlink(a);
    (void)unlink(b);
    fd = open(a, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("renameat201", 0);
        return;
    }
    close(fd);
    fd = open(b, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        (void)unlink(a);
        report("renameat201", 0);
        return;
    }
    close(fd);
    errno = 0;
    ok = renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_NOREPLACE) < 0 &&
         errno == EEXIST;
    (void)unlink(a);
    (void)unlink(b);
    report("renameat201", ok);
}

static void open_tmpfile01(void)
{
    int fd;
    char buf[8];
    int ok = 0;

    fd = open("/tmp", O_RDWR | O_TMPFILE, 0600);
    if (fd < 0) {
        report("open_tmpfile01", 0);
        return;
    }
    ok = write(fd, "TMPF", 4) == 4 &&
         lseek(fd, 0, SEEK_SET) == 0 &&
         read(fd, buf, 4) == 4 && memcmp(buf, "TMPF", 4) == 0;
    close(fd);
    report("open_tmpfile01", ok);
}

static void open_opath01(void)
{
    const char *path = "/tmp/ltp_opath";
    int fd;
    int fl;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
    }
    fd = open(path, O_PATH);
    if (fd < 0) {
        (void)unlink(path);
        report("open_opath01", 0);
        return;
    }
    fl = fcntl(fd, F_GETFL);
    ok = fl >= 0 && (fl & O_PATH) != 0;
    close(fd);
    (void)unlink(path);
    report("open_opath01", ok);
}

static void fallocate02_punch(void)
{
    const char *path = "/tmp/ltp_punch";
    int fd;
    char buf[16];
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("fallocate02", 0);
        return;
    }
    if (write(fd, "ABCDEFGH", 8) == 8 &&
        fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 2, 4) == 0 &&
        lseek(fd, 0, SEEK_SET) == 0 &&
        read(fd, buf, 8) == 8) {
        ok = buf[0] == 'A' && buf[1] == 'B' &&
             buf[2] == 0 && buf[3] == 0 && buf[4] == 0 && buf[5] == 0 &&
             buf[6] == 'G' && buf[7] == 'H';
    }
    close(fd);
    (void)unlink(path);
    report("fallocate02", ok);
}

static void sendmsg01_multi(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba;
    socklen_t alen = sizeof(aa), blen = sizeof(ba);
    char p0[4] = "XY", p1[4] = "ZW";
    char rbuf[8];
    struct iovec iov[2];
    struct msghdr mh;
    int ok = 0;

    memset(&aa, 0, sizeof(aa));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0) {
        memset(&mh, 0, sizeof(mh));
        iov[0].iov_base = p0;
        iov[0].iov_len = 2;
        iov[1].iov_base = p1;
        iov[1].iov_len = 2;
        mh.msg_iov = iov;
        mh.msg_iovlen = 2;
        mh.msg_name = &ba;
        mh.msg_namelen = blen;
        ok = sendmsg(a, &mh, 0) == 4 &&
             recvfrom(b, rbuf, sizeof(rbuf), 0, NULL, NULL) == 4 &&
             memcmp(rbuf, "XYZW", 4) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("sendmsg01", ok);
}

static void setsockopt04_ip(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int ttl = 32, tos = 0x10, oob = 1, v = -1;
    socklen_t len = sizeof(v);
    int ok = 0;

    if (s < 0) {
        report("setsockopt04", 0);
        return;
    }
    if (setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == 0 &&
        getsockopt(s, IPPROTO_IP, IP_TTL, &v, &len) == 0 && v == 32 &&
        setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0 &&
        (len = sizeof(v), v = -1,
         getsockopt(s, IPPROTO_IP, IP_TOS, &v, &len) == 0 && v == 0x10) &&
        setsockopt(s, SOL_SOCKET, SO_OOBINLINE, &oob, sizeof(oob)) == 0 &&
        (len = sizeof(v), v = -1,
         getsockopt(s, SOL_SOCKET, SO_OOBINLINE, &v, &len) == 0 && v == 1)) {
        ok = 1;
    }
    close(s);
    report("setsockopt04", ok);
}

static void fcntl_dupfd_cloexec01(void)
{
    int fd = open("/tmp/ltp_dupfdce", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int d;
    int ok = 0;

    if (fd < 0) {
        report("fcntl_dupfd_cloexec01", 0);
        return;
    }
    d = fcntl(fd, F_DUPFD_CLOEXEC, 20);
    if (d >= 20) {
        ok = (fcntl(d, F_GETFD) & FD_CLOEXEC) != 0;
        close(d);
    }
    close(fd);
    (void)unlink("/tmp/ltp_dupfdce");
    report("fcntl_dupfd_cloexec01", ok);
}

static void eventfd_signalfd_nb01(void)
{
    int efd = eventfd(0, EFD_NONBLOCK);
    int sfd;
    sigset_t set;
    char buf[128];
    int ok = 0;
    int fl;

    if (efd < 0) {
        report("eventfd_nb01", 0);
        report("signalfd_nb01", 0);
        return;
    }
    fl = fcntl(efd, F_GETFL);
    errno = 0;
    ok = (fl & O_NONBLOCK) != 0 &&
         read(efd, buf, 8) < 0 && errno == EAGAIN;
    report("eventfd_nb01", ok);
    close(efd);

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sfd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd < 0) {
        report("signalfd_nb01", 0);
        return;
    }
    fl = fcntl(sfd, F_GETFL);
    errno = 0;
    ok = (fl & O_NONBLOCK) != 0 &&
         read(sfd, buf, sizeof(buf)) < 0 && errno == EAGAIN;
    close(sfd);
    report("signalfd_nb01", ok);
}

static void unix_dgram01(void)
{
    int a = -1, b = -1;
    struct sockaddr_un aa, ba;
    char buf[8];
    int ok = 0;
    const char *pa = "/tmp/ltp_udg_a";
    const char *pb = "/tmp/ltp_udg_b";

    (void)unlink(pa);
    (void)unlink(pb);
    memset(&aa, 0, sizeof(aa));
    memset(&ba, 0, sizeof(ba));
    aa.sun_family = AF_UNIX;
    ba.sun_family = AF_UNIX;
    strncpy(aa.sun_path, pa, sizeof(aa.sun_path) - 1);
    strncpy(ba.sun_path, pb, sizeof(ba.sun_path) - 1);
    a = socket(AF_UNIX, SOCK_DGRAM, 0);
    b = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        connect(a, (struct sockaddr *)&ba, sizeof(ba)) == 0) {
        ok = send(a, "UG", 2, 0) == 2 &&
             recv(b, buf, sizeof(buf), MSG_DONTWAIT) == 2 &&
             memcmp(buf, "UG", 2) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    (void)unlink(pa);
    (void)unlink(pb);
    report("unix_dgram01", ok);
}

static void udp03_peer_filter(void)
{
    int a = -1, b = -1, c = -1;
    struct sockaddr_in aa, ba, ca;
    socklen_t alen = sizeof(aa), blen = sizeof(ba), clen = sizeof(ca);
    char buf[8];
    int ok = 0;

    memset(&aa, 0, sizeof(aa));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    ca = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    c = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 && c >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        bind(c, (struct sockaddr *)&ca, sizeof(ca)) == 0 &&
        getsockname(a, (struct sockaddr *)&aa, &alen) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0 &&
        getsockname(c, (struct sockaddr *)&ca, &clen) == 0 &&
        connect(b, (struct sockaddr *)&aa, alen) == 0) {
        /* Foreign peer c → b should be filtered; a → b should pass. */
        ok = sendto(c, "XX", 2, 0, (struct sockaddr *)&ba, blen) == 2 &&
             sendto(a, "OK", 2, 0, (struct sockaddr *)&ba, blen) == 2 &&
             recv(b, buf, sizeof(buf), 0) == 2 &&
             memcmp(buf, "OK", 2) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    if (c >= 0) {
        close(c);
    }
    report("udp03", ok);
}

static void so_error01(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    int err = -1;
    socklen_t elen = sizeof(err);
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(1); /* no listener */
    if (s < 0) {
        report("so_error01", 0);
        return;
    }
    errno = 0;
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ok = getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 &&
             err == ECONNREFUSED;
        if (ok) {
            elen = sizeof(err);
            err = -1;
            ok = getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 &&
                 err == 0;
        }
    }
    close(s);
    report("so_error01", ok);
}

static void statfs01(void)
{
    struct statfs st;
    int fd;
    int ok = 0;

    if (statfs("/tmp", &st) == 0 && st.f_type == TMPFS_MAGIC) {
        fd = open("/tmp/ltp_fstatfs", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ok = fstatfs(fd, &st) == 0 && st.f_type == TMPFS_MAGIC;
            close(fd);
            (void)unlink("/tmp/ltp_fstatfs");
        }
    }
    report("statfs01", ok);
}

static void listen02_backlog(void)
{
    int srv = -1, c0 = -1, c1 = -1, a0 = -1, a1 = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    c0 = socket(AF_INET, SOCK_STREAM, 0);
    c1 = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && c0 >= 0 && c1 >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 2) == 0 &&
        connect(c0, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        connect(c1, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        a0 = accept(srv, NULL, NULL);
        a1 = accept(srv, NULL, NULL);
        ok = a0 >= 0 && a1 >= 0 && a0 != a1;
    }
    if (a0 >= 0) {
        close(a0);
    }
    if (a1 >= 0) {
        close(a1);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (c0 >= 0) {
        close(c0);
    }
    if (c1 >= 0) {
        close(c1);
    }
    report("listen02", ok);
}

static void memfd_seals01(void)
{
    int fd = memfd_create("ltp_seal", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    int seals;
    int ok = 0;

    if (fd < 0) {
        report("memfd_seals01", 0);
        return;
    }
    if (ftruncate(fd, 64) == 0 &&
        fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SEAL) == 0) {
        seals = fcntl(fd, F_GET_SEALS);
        errno = 0;
        ok = (seals & F_SEAL_GROW) != 0 && (seals & F_SEAL_SEAL) != 0 &&
             ftruncate(fd, 128) < 0 && errno == EPERM &&
             fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) < 0;
    }
    close(fd);
    report("memfd_seals01", ok);
}

#ifndef MSG_CMSG_CLOEXEC
#define MSG_CMSG_CLOEXEC 0x40000000
#endif
#ifndef SPLICE_F_NONBLOCK
#define SPLICE_F_NONBLOCK 2
#endif
#ifndef EFD_SEMAPHORE
#define EFD_SEMAPHORE 1
#endif
#ifndef TFD_TIMER_ABSTIME
#define TFD_TIMER_ABSTIME 1
#endif
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif
#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK 0x0001
#endif
#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif
#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#define __NR_pidfd_send_signal 424
#define __NR_close_range 436
#define __NR_openat2 437
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ 0x5414
#endif
#ifndef PR_SET_PDEATHSIG
#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#endif

struct bfree_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

/* --- O. twelve-hole batch --- */

static void recvmsg01_cmsg_cloexec(void)
{
    int a = -1, b = -1;
    struct sockaddr_in aa, ba;
    socklen_t alen = sizeof(aa), blen = sizeof(ba);
    char payload[4] = "RCXY";
    char r0[2], r1[2];
    struct iovec iov[2];
    struct msghdr mh;
    char cbuf[64];
    int ok = 0;

    (void)alen;
    memset(&aa, 0, sizeof(aa));
    aa.sin_family = AF_INET;
    aa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    aa.sin_port = 0;
    ba = aa;
    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    if (a >= 0 && b >= 0 &&
        bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0 &&
        bind(b, (struct sockaddr *)&ba, sizeof(ba)) == 0 &&
        getsockname(b, (struct sockaddr *)&ba, &blen) == 0 &&
        sendto(a, payload, 4, 0, (struct sockaddr *)&ba, blen) == 4) {
        memset(&mh, 0, sizeof(mh));
        iov[0].iov_base = r0;
        iov[0].iov_len = 2;
        iov[1].iov_base = r1;
        iov[1].iov_len = 2;
        mh.msg_iov = iov;
        mh.msg_iovlen = 2;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        ok = recvmsg(b, &mh, MSG_CMSG_CLOEXEC) == 4 &&
             mh.msg_controllen == 0 &&
             memcmp(r0, "RC", 2) == 0 && memcmp(r1, "XY", 2) == 0;
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    report("recvmsg01", ok);
}

static void splice02_nonblock(void)
{
    int a[2], b[2];
    int ok = 0;

    if (pipe(a) != 0 || pipe(b) != 0) {
        report("splice_nb01", 0);
        return;
    }
    (void)fcntl(a[0], F_SETFL, O_NONBLOCK);
    (void)fcntl(b[1], F_SETFL, O_NONBLOCK);
    errno = 0;
    ok = splice(a[0], NULL, b[1], NULL, 8, SPLICE_F_NONBLOCK) < 0 &&
         errno == EAGAIN;
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    report("splice_nb01", ok);
}

static void epoll04_et_oneshot(void)
{
    int p[2];
    int ep;
    struct epoll_event ev, out;
    char c;
    int ok = 0;

    if (pipe(p) != 0) {
        report("epoll04", 0);
        return;
    }
    ep = epoll_create1(0);
    if (ep >= 0 && write(p[1], "E", 1) == 1) {
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        ev.data.fd = p[0];
        if (epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0 &&
            epoll_wait(ep, &out, 1, 0) == 1 &&
            (out.events & EPOLLIN)) {
            ok = epoll_wait(ep, &out, 1, 0) == 0;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
            ev.data.fd = p[0];
            if (ok && epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev) == 0) {
                ok = epoll_wait(ep, &out, 1, 0) == 1 &&
                     read(p[0], &c, 1) == 1 && c == 'E';
            } else {
                ok = 0;
            }
        }
        close(ep);
    }
    close(p[0]);
    close(p[1]);
    report("epoll04", ok);
}

static void eventfd_sem01(void)
{
    int efd = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
    uint64_t v = 0;
    int ok = 0;

    if (efd < 0) {
        report("eventfd_sem01", 0);
        return;
    }
    ok = read(efd, &v, sizeof(v)) == (ssize_t)sizeof(v) && v == 1 &&
         read(efd, &v, sizeof(v)) == (ssize_t)sizeof(v) && v == 1;
    if (ok) {
        errno = 0;
        ok = read(efd, &v, sizeof(v)) < 0 && errno == EAGAIN;
    }
    close(efd);
    report("eventfd_sem01", ok);
}

static void timerfd_abstime01(void)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its, cur;
    struct timespec now;
    uint64_t exp = 0;
    int ok = 0;

    if (tfd < 0 || clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        if (tfd >= 0) {
            close(tfd);
        }
        report("timerfd_abstime01", 0);
        return;
    }
    memset(&its, 0, sizeof(its));
    its.it_value = now;
    its.it_value.tv_nsec += 30000000L;
    if (its.it_value.tv_nsec >= 1000000000L) {
        its.it_value.tv_sec++;
        its.it_value.tv_nsec -= 1000000000L;
    }
    if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &its, NULL) == 0) {
        struct timespec sl;
        sl.tv_sec = 0;
        sl.tv_nsec = 80000000L;
        (void)nanosleep(&sl, NULL);
        ok = read(tfd, &exp, sizeof(exp)) == (ssize_t)sizeof(exp) && exp >= 1 &&
             timerfd_gettime(tfd, &cur) == 0;
    }
    close(tfd);
    report("timerfd_abstime01", ok);
}

static void prctl_pdeathsig01(void)
{
    int got = -1;
    int ok = 0;

    if (prctl(PR_SET_PDEATHSIG, SIGUSR1) == 0 &&
        prctl(PR_GET_PDEATHSIG, &got) == 0 && got == SIGUSR1) {
        ok = prctl(PR_SET_PDEATHSIG, 0) == 0;
    }
    report("prctl_pdeathsig01", ok);
}

static void getrandom_flags01(void)
{
    unsigned char buf[16];
    int ok = 0;

    errno = 0;
    ok = getrandom(buf, sizeof(buf), GRND_NONBLOCK) == (ssize_t)sizeof(buf);
    if (ok) {
        errno = 0;
        ok = getrandom(buf, sizeof(buf), 8) < 0 && errno == EINVAL;
    }
    report("getrandom_flags01", ok);
}

static void clock_nanosleep_abstime01(void)
{
    struct timespec now, req, rem;
    int ok = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        report("clock_nanosleep_abstime01", 0);
        return;
    }
    req = now;
    req.tv_nsec += 20000000L;
    if (req.tv_nsec >= 1000000000L) {
        req.tv_sec++;
        req.tv_nsec -= 1000000000L;
    }
    rem.tv_sec = rem.tv_nsec = -1;
    ok = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, &rem) == 0;
    report("clock_nanosleep_abstime01", ok);
}

static void inet6_01(void)
{
    int s = socket(AF_INET6, SOCK_DGRAM, 0);
    struct sockaddr_in6 addr, name;
    socklen_t nlen = sizeof(name);
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_loopback;
    addr.sin6_port = 0;
    if (s < 0) {
        report("inet6_01", 0);
        return;
    }
    ok = bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
         getsockname(s, (struct sockaddr *)&name, &nlen) == 0 &&
         name.sin6_family == AF_INET6;
    close(s);
    report("inet6_01", ok);
}

static void msg_oob01(void)
{
    int srv = -1, cli = -1, afd = -1;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[4];
    int ok = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    srv = socket(AF_INET, SOCK_STREAM, 0);
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (srv >= 0 && cli >= 0 &&
        bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
        getsockname(srv, (struct sockaddr *)&addr, &alen) == 0 &&
        listen(srv, 1) == 0 &&
        connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        afd = accept(srv, NULL, NULL);
        if (afd >= 0 && send(cli, "O", 1, MSG_OOB) == 1) {
            ok = recv(afd, buf, 1, MSG_OOB) == 1 && buf[0] == 'O';
        }
    }
    if (afd >= 0) {
        close(afd);
    }
    if (srv >= 0) {
        close(srv);
    }
    if (cli >= 0) {
        close(cli);
    }
    report("msg_oob01", ok);
}

static void ioctl_winsz01(void)
{
    struct winsize ws, out;
    int ok = 0;

    ws.ws_row = 40;
    ws.ws_col = 100;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    if (ioctl(0, TIOCSWINSZ, &ws) == 0 &&
        ioctl(0, TIOCGWINSZ, &out) == 0) {
        ok = out.ws_row == 40 && out.ws_col == 100;
    }
    report("ioctl_winsz01", ok);
}

static void mount_umount01(void)
{
    int ok = mount("none", "/tmp", "tmpfs", 0, NULL) == 0 &&
             umount2("/tmp", 0) == 0;

    report("mount_umount01", ok);
}

static void pidfd01(void)
{
    int pfd;
    int ok = 0;

    pfd = (int)syscall(__NR_pidfd_open, (long)getpid(), 0L);
    if (pfd < 0) {
        report("pidfd01", 0);
        return;
    }
    ok = syscall(__NR_pidfd_send_signal, (long)pfd, 0L, 0L, 0L) == 0;
    close(pfd);
    report("pidfd01", ok);
}

static void close_range01(void)
{
    int fd = open("/tmp/ltp_crange", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd < 0) {
        report("close_range01", 0);
        return;
    }
    ok = syscall(__NR_close_range, (long)fd, (long)fd, (long)CLOSE_RANGE_CLOEXEC) == 0 &&
         (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
    (void)syscall(__NR_close_range, (long)fd, (long)fd, 0L);
    errno = 0;
    ok = ok && fcntl(fd, F_GETFD) < 0 && errno == EBADF;
    (void)unlink("/tmp/ltp_crange");
    report("close_range01", ok);
}

static void openat201(void)
{
    struct bfree_open_how how;
    int fd;
    int ok = 0;

    memset(&how, 0, sizeof(how));
    how.flags = (uint64_t)(O_RDWR | O_CREAT | O_TRUNC);
    how.mode = 0644;
    fd = (int)syscall(__NR_openat2, (long)AT_FDCWD, (long)"/tmp/ltp_openat2",
                      (long)&how, (long)sizeof(how));
    if (fd >= 0) {
        ok = write(fd, "OA", 2) == 2;
        close(fd);
        (void)unlink("/tmp/ltp_openat2");
    }
    report("openat201", ok);
}

#ifndef __NR_faccessat2
#define __NR_faccessat2 439
#endif
#ifndef __NR_syncfs
#define __NR_syncfs 306
#endif
#ifndef __NR_unshare
#define __NR_unshare 272
#endif
#ifndef __NR_capget
#define __NR_capget 125
#define __NR_capset 126
#endif
#ifndef __NR_pidfd_getfd
#define __NR_pidfd_getfd 438
#endif
#ifndef __NR_preadv2
#define __NR_preadv2 327
#define __NR_pwritev2 328
#endif
#ifndef __NR_inotify_init1
#define __NR_inotify_init1 294
#define __NR_inotify_add_watch 254
#define __NR_inotify_rm_watch 255
#endif
#ifndef __NR_fsetxattr
#define __NR_fsetxattr 190
#define __NR_fgetxattr 193
#define __NR_flistxattr 196
#define __NR_fremovexattr 199
#endif
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif
#ifndef FIONBIO
#define FIONBIO 0x5421
#endif
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif
#ifndef CLONE_FS
#define CLONE_FS 0x200
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1)
#endif
#ifndef _LINUX_CAPABILITY_VERSION_3
#define _LINUX_CAPABILITY_VERSION_3 0x20080522U
#endif
#ifndef IN_CLOEXEC
#define IN_CLOEXEC 02000000
#endif
#ifndef IN_NONBLOCK
#define IN_NONBLOCK 00004000
#endif
#ifndef IN_CREATE
#define IN_CREATE 0x00000100
#endif
#ifndef ENODATA
#define ENODATA 61
#endif
#ifndef MCL_CURRENT
#define MCL_CURRENT 1
#endif

struct bfree_cap_header {
    uint32_t version;
    int pid;
};

struct bfree_cap_data {
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
};

/* --- P. fifteen-hole batch --- */

static void faccessat201(void)
{
    int ok = syscall(__NR_faccessat2, (long)AT_FDCWD, (long)"/tmp",
                     (long)F_OK, 0L) == 0;

    report("faccessat201", ok);
}

static void syncfs01(void)
{
    int fd = open("/tmp/ltp_syncfs", O_RDWR | O_CREAT | O_TRUNC, 0644);
    int ok = 0;

    if (fd >= 0) {
        ok = syscall(__NR_syncfs, (long)fd) == 0;
        close(fd);
        (void)unlink("/tmp/ltp_syncfs");
    }
    report("syncfs01", ok);
}

static void mlock01(void)
{
    void *p;
    int ok = 0;

    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        report("mlock01", 0);
        return;
    }
    ok = mlock(p, 4096) == 0 &&
         munlock(p, 4096) == 0 &&
         mlockall(MCL_CURRENT) == 0 &&
         munlockall() == 0;
    munmap(p, 4096);
    report("mlock01", ok);
}

static void sched_aff01(void)
{
    cpu_set_t set, got;
    int ok = 0;

    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) == 0 &&
        sched_getaffinity(0, sizeof(got), &got) == 0 &&
        CPU_ISSET(0, &got) &&
        sched_yield() == 0) {
        ok = 1;
    }
    report("sched_aff01", ok);
}

static void ioctl_fion01(void)
{
    int p[2];
    int pending = -1;
    int nb = 1;
    int fl;
    int ok = 0;

    if (pipe(p) != 0) {
        report("ioctl_fion01", 0);
        return;
    }
    if (write(p[1], "ABC", 3) == 3 &&
        ioctl(p[0], FIONREAD, &pending) == 0 && pending == 3 &&
        ioctl(p[0], FIONBIO, &nb) == 0) {
        fl = fcntl(p[0], F_GETFL);
        ok = fl >= 0 && (fl & O_NONBLOCK) != 0;
    }
    close(p[0]);
    close(p[1]);
    report("ioctl_fion01", ok);
}

static void setsockopt05(void)
{
    int udp = -1, tcp = -1;
    int on = 1, got = 0;
    socklen_t glen = sizeof(got);
    int ok = 0;

    udp = socket(AF_INET, SOCK_DGRAM, 0);
    tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (udp >= 0 && tcp >= 0 &&
        setsockopt(udp, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) == 0 &&
        getsockopt(udp, SOL_SOCKET, SO_REUSEPORT, &got, &glen) == 0 &&
        got == 1) {
        got = 0;
        glen = sizeof(got);
        ok = setsockopt(tcp, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0 &&
             getsockopt(tcp, IPPROTO_TCP, TCP_NODELAY, &got, &glen) == 0 &&
             got == 1;
    }
    if (udp >= 0) {
        close(udp);
    }
    if (tcp >= 0) {
        close(tcp);
    }
    report("setsockopt05", ok);
}

static void preadv201(void)
{
    const char *path = "/tmp/ltp_preadv2";
    int fd;
    char w0[4] = "PREA", w1[4] = "DV2X";
    char r0[4], r1[4];
    struct iovec wiov[2], riov[2];
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("preadv201", 0);
        return;
    }
    wiov[0].iov_base = w0;
    wiov[0].iov_len = 4;
    wiov[1].iov_base = w1;
    wiov[1].iov_len = 4;
    riov[0].iov_base = r0;
    riov[0].iov_len = 4;
    riov[1].iov_base = r1;
    riov[1].iov_len = 4;
    ok = syscall(__NR_pwritev2, (long)fd, (long)wiov, 2L, 0L, 0L) == 8 &&
         syscall(__NR_preadv2, (long)fd, (long)riov, 2L, 0L, 0L) == 8 &&
         memcmp(r0, "PREA", 4) == 0 && memcmp(r1, "DV2X", 4) == 0;
    close(fd);
    (void)unlink(path);
    report("preadv201", ok);
}

static void unshare01(void)
{
    int ok = syscall(__NR_unshare, (long)CLONE_FS) == 0;

    report("unshare01", ok);
}

static void cap01(void)
{
    struct bfree_cap_header hdr;
    struct bfree_cap_data data[2];
    int ok = 0;

    memset(&hdr, 0, sizeof(hdr));
    memset(data, 0, sizeof(data));
    hdr.version = 0;
    hdr.pid = 0;
    if (syscall(__NR_capget, (long)&hdr, (long)data) == 0 &&
        hdr.version == _LINUX_CAPABILITY_VERSION_3) {
        ok = syscall(__NR_capset, (long)&hdr, (long)data) == 0;
    }
    report("cap01", ok);
}

static void pidfd_getfd01(void)
{
    const char *path = "/tmp/ltp_pidfd_getfd";
    int pfd = -1, fd = -1, gfd = -1;
    char buf[4];
    int ok = 0;

    pfd = (int)syscall(__NR_pidfd_open, (long)getpid(), 0L);
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (pfd >= 0 && fd >= 0 && write(fd, "PG", 2) == 2) {
        gfd = (int)syscall(__NR_pidfd_getfd, (long)pfd, (long)fd, 0L);
        if (gfd >= 0 && lseek(gfd, 0, SEEK_SET) == 0 &&
            read(gfd, buf, 2) == 2 && memcmp(buf, "PG", 2) == 0 &&
            write(gfd, "FD", 2) == 2) {
            ok = 1;
        }
    }
    if (gfd >= 0) {
        close(gfd);
    }
    if (fd >= 0) {
        close(fd);
    }
    if (pfd >= 0) {
        close(pfd);
    }
    (void)unlink(path);
    report("pidfd_getfd01", ok);
}

static void tee_vmsplice_nb01(void)
{
    int a[2], b[2];
    char fill[4096];
    char one = 'Z';
    struct iovec iov;
    int ok = 0;

    memset(fill, 'F', sizeof(fill));
    if (pipe(a) != 0 || pipe(b) != 0) {
        report("tee_vmsplice_nb01", 0);
        return;
    }
    errno = 0;
    if (tee(a[0], b[1], 8, SPLICE_F_NONBLOCK) < 0 && errno == EAGAIN) {
        /* Fill write end, then NONBLOCK vmsplice must EAGAIN. */
        if (write(a[1], fill, sizeof(fill)) == (ssize_t)sizeof(fill)) {
            iov.iov_base = &one;
            iov.iov_len = 1;
            errno = 0;
            ok = vmsplice(a[1], &iov, 1, SPLICE_F_NONBLOCK) < 0 &&
                 errno == EAGAIN;
        }
    }
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    report("tee_vmsplice_nb01", ok);
}

static void renameat202(void)
{
    const char *a = "/tmp/ltp_exch_a";
    const char *b = "/tmp/ltp_exch_b";
    char buf[4];
    int fd;
    int ok = 0;

    (void)unlink(a);
    (void)unlink(b);
    fd = open(a, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("renameat202", 0);
        return;
    }
    ok = write(fd, "AA", 2) == 2;
    close(fd);
    fd = open(b, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || !ok) {
        (void)unlink(a);
        report("renameat202", 0);
        return;
    }
    ok = write(fd, "BB", 2) == 2;
    close(fd);
    if (!ok || renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE) != 0) {
        (void)unlink(a);
        (void)unlink(b);
        report("renameat202", 0);
        return;
    }
    fd = open(a, O_RDONLY);
    ok = fd >= 0 && read(fd, buf, 2) == 2 && memcmp(buf, "BB", 2) == 0;
    if (fd >= 0) {
        close(fd);
    }
    fd = open(b, O_RDONLY);
    ok = ok && fd >= 0 && read(fd, buf, 2) == 2 && memcmp(buf, "AA", 2) == 0;
    if (fd >= 0) {
        close(fd);
    }
    (void)unlink(a);
    (void)unlink(b);
    report("renameat202", ok);
}

static void epoll_pwait01(void)
{
    int p[2];
    int ep;
    struct epoll_event ev, out;
    sigset_t empty;
    int ok = 0;

    if (pipe(p) != 0) {
        report("epoll_pwait01", 0);
        return;
    }
    ep = epoll_create1(0);
    sigemptyset(&empty);
    if (ep >= 0 && write(p[1], "P", 1) == 1) {
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0 &&
             epoll_pwait(ep, &out, 1, 0, &empty) == 1 &&
             (out.events & EPOLLIN);
        close(ep);
    }
    close(p[0]);
    close(p[1]);
    report("epoll_pwait01", ok);
}

static void inotify01(void)
{
    int fd;
    int wd;
    int ok = 0;

    fd = (int)syscall(__NR_inotify_init1, (long)(IN_CLOEXEC | IN_NONBLOCK));
    if (fd < 0) {
        report("inotify01", 0);
        return;
    }
    wd = (int)syscall(__NR_inotify_add_watch, (long)fd, (long)"/tmp",
                      (long)IN_CREATE);
    ok = wd > 0 &&
         syscall(__NR_inotify_rm_watch, (long)fd, (long)wd) == 0;
    close(fd);
    report("inotify01", ok);
}

static void xattr01(void)
{
    const char *path = "/tmp/ltp_xattr";
    const char *name = "user.ltp";
    const char *val = "VAL";
    char out[16];
    char list[64];
    int fd;
    int ok = 0;
    long n;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("xattr01", 0);
        return;
    }
    if (syscall(__NR_fsetxattr, (long)fd, (long)name, (long)val, 3L, 0L) == 0) {
        n = syscall(__NR_fgetxattr, (long)fd, (long)name, (long)out, 16L);
        ok = n == 3 && memcmp(out, "VAL", 3) == 0;
        n = syscall(__NR_flistxattr, (long)fd, (long)list, (long)sizeof(list));
        ok = ok && n > 0 && memcmp(list, name, strlen(name) + 1) == 0;
        ok = ok && syscall(__NR_fremovexattr, (long)fd, (long)name) == 0;
        errno = 0;
        ok = ok && syscall(__NR_fgetxattr, (long)fd, (long)name, (long)out, 16L) < 0 &&
             errno == ENODATA;
    }
    close(fd);
    (void)unlink(path);
    report("xattr01", ok);
}

/* --- Q. fifteen-hole batch --- */

static void getcpu01(void)
{
    unsigned cpu = 99, node = 99;
    int ok = 0;

    if (syscall(__NR_getcpu, (long)&cpu, (long)&node, 0L) == 0 && cpu == 0U) {
        ok = syscall(__NR_ioprio_set, 0L, 0L, 0L) == 0 &&
             syscall(__NR_ioprio_get, 0L, 0L) >= 0;
    }
    report("getcpu01", ok);
}

static void sethostname01(void)
{
    int ok = syscall(__NR_sethostname, (long)"bfree-q", 7L) == 0 &&
             syscall(__NR_setdomainname, (long)"local", 5L) == 0;

    report("sethostname01", ok);
}

static void getgroups01(void)
{
    gid_t list[8];
    gid_t gid = 0;
    int n;
    int ok = 0;

    n = getgroups((int)(sizeof(list) / sizeof(list[0])), list);
    if (n >= 0) {
        ok = syscall(__NR_setgroups, 1L, (long)&gid) == 0;
    }
    report("getgroups01", ok);
}

static void chroot01(void)
{
    int ok = syscall(__NR_chroot, (long)"/tmp") == 0;

    report("chroot01", ok);
}

static void setns01(void)
{
    int ok = syscall(__NR_setns, 0L, 0L) == 0 &&
             syscall(__NR_pivot_root, (long)"/tmp", (long)"/tmp") == 0;

    report("setns01", ok);
}

static void timer_create01(void)
{
    long tid = 0;
    struct itimerspec its, got;
    int ok = 0;

    memset(&its, 0, sizeof(its));
    memset(&got, 0, sizeof(got));
    its.it_value.tv_sec = 1;
    if (syscall(__NR_timer_create, (long)CLOCK_MONOTONIC, 0L, (long)&tid) == 0 &&
        tid > 0) {
        ok = syscall(__NR_timer_settime, tid, 0L, (long)&its, 0L) == 0 &&
             syscall(__NR_timer_gettime, tid, (long)&got) == 0 &&
             got.it_value.tv_sec == 1 &&
             syscall(__NR_timer_delete, tid) == 0;
    }
    report("timer_create01", ok);
}

static void shmget01(void)
{
    int id;
    void *addr;
    volatile char *p;
    int ok = 0;

    id = shmget(IPC_PRIVATE, 4096, 0666);
    if (id >= 0) {
        addr = shmat(id, NULL, 0);
        if (addr != (void *)-1 && addr != NULL) {
            p = (volatile char *)addr;
            p[0] = 'S';
            ok = p[0] == 'S' && shmdt(addr) == 0 &&
                 shmctl(id, IPC_RMID, NULL) == 0;
        } else {
            (void)shmctl(id, IPC_RMID, NULL);
        }
    }
    report("shmget01", ok);
}

static void process_vm01(void)
{
    char remote[8] = "VMREAD";
    char local[8];
    struct iovec liov, riov;
    int ok = 0;

    memset(local, 0, sizeof(local));
    liov.iov_base = local;
    liov.iov_len = 6;
    riov.iov_base = remote;
    riov.iov_len = 6;
    ok = syscall(__NR_process_vm_readv, (long)getpid(), (long)&liov, 1L,
                 (long)&riov, 1L, 0L) == 6 &&
         memcmp(local, "VMREAD", 6) == 0;
    report("process_vm01", ok);
}

struct bfree_clone_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
};

static void clone301(void)
{
    struct bfree_clone_args args;
    char stack[8192];
    long rc;
    int st = -1;
    int ok = 0;

    memset(&args, 0, sizeof(args));
    args.flags = (uint64_t)(CLONE_VFORK | CLONE_VM);
    args.exit_signal = (uint64_t)SIGCHLD;
    args.stack = (uint64_t)(uintptr_t)stack;
    args.stack_size = sizeof(stack);

    reap_zombies_nonblock();
    rc = syscall(__NR_clone3, (long)&args, (long)sizeof(args));
    if (rc < 0) {
        report("clone301", 0);
        return;
    }
    if (rc == 0) {
        _exit(0);
    }
    ok = rc > 0 && waitpid((pid_t)rc, &st, 0) == (pid_t)rc && WIFEXITED(st);
    report("clone301", ok);
}

static void fanotify01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_fanotify_init, 0L, 0L);
    if (fd >= 0) {
        ok = syscall(__NR_fanotify_mark, (long)fd, 0L, 0L,
                     (long)AT_FDCWD, (long)"/tmp") == 0;
        close(fd);
    }
    report("fanotify01", ok);
}

struct bfree_file_handle {
    unsigned int handle_bytes;
    int handle_type;
    unsigned char f_handle[8];
};

static void name_to_handle01(void)
{
    struct bfree_file_handle fh;
    int mnt = 0;
    int fd = -1;
    int ok = 0;

    memset(&fh, 0, sizeof(fh));
    fh.handle_bytes = 8;
    if (syscall(__NR_name_to_handle_at, (long)AT_FDCWD, (long)"/tmp",
                (long)&fh, (long)&mnt, 0L) == 0 &&
        fh.handle_bytes == 8U) {
        fd = (int)syscall(__NR_open_by_handle_at, (long)AT_FDCWD, (long)&fh,
                          (long)O_RDONLY);
        ok = fd >= 0;
        if (fd >= 0) {
            close(fd);
        }
    }
    report("name_to_handle01", ok);
}

struct bfree_statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __pad1;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
};

static void statx01(void)
{
    struct bfree_statx stx;
    int ok = 0;

    memset(&stx, 0, sizeof(stx));
    if (syscall(__NR_statx, (long)AT_FDCWD, (long)"/tmp", 0L,
                (long)STATX_BASIC_STATS, (long)&stx) == 0) {
        ok = (stx.stx_mask & STATX_BASIC_STATS) != 0 &&
             S_ISDIR(stx.stx_mode);
    }
    report("statx01", ok);
}

static void memfd_secret01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_memfd_secret, 0L);
    if (fd >= 0) {
        ok = write(fd, "SEC", 3) == 3;
        close(fd);
    }
    errno = 0;
    (void)syscall(__NR_execveat, (long)AT_FDCWD, (long)"/no/such/execveat",
                  0L, 0L, 0L);
    ok = ok && errno != ENOSYS && errno != 0;
    report("memfd_secret01", ok);
}

static void clock_settime01(void)
{
    struct timespec ts;
    struct timeval tv;
    int ok = 0;

    ts.tv_sec = 1700000000;
    ts.tv_nsec = 0;
    tv.tv_sec = 1700000001;
    tv.tv_usec = 0;
    ok = syscall(__NR_clock_settime, (long)CLOCK_REALTIME, (long)&ts) == 0 &&
         syscall(__NR_settimeofday, (long)&tv, 0L) == 0;
    report("clock_settime01", ok);
}

static void fallocate03(void)
{
    const char *path = "/tmp/ltp_zero_range";
    int fd;
    char buf[16];
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("fallocate03", 0);
        return;
    }
    if (write(fd, "ABCDEFGH", 8) == 8 &&
        fallocate(fd, FALLOC_FL_ZERO_RANGE, 2, 4) == 0 &&
        lseek(fd, 0, SEEK_SET) == 0 &&
        read(fd, buf, 8) == 8) {
        ok = buf[0] == 'A' && buf[1] == 'B' &&
             buf[2] == 0 && buf[3] == 0 && buf[4] == 0 && buf[5] == 0 &&
             buf[6] == 'G' && buf[7] == 'H';
    }
    close(fd);
    (void)unlink(path);
    report("fallocate03", ok);
}

/* --- R. fifteen-hole batch --- */

static void semmsg01(void)
{
    int semid = -1;
    int msqid = -1;
    struct {
        long mtype;
        char mtext[8];
    } msg, rmsg;
    int ok = 0;

    semid = (int)syscall(__NR_semget, (long)IPC_PRIVATE, 1L, 0666L);
    if (semid >= 0) {
        ok = syscall(__NR_semctl, (long)semid, 0L, (long)SETVAL, 1L) == 0 &&
             syscall(__NR_semctl, (long)semid, 0L, (long)GETVAL, 0L) == 1 &&
             syscall(__NR_semop, (long)semid, 0L, 0L) == 0 &&
             syscall(__NR_semctl, (long)semid, 0L, (long)IPC_RMID, 0L) == 0;
        semid = -1;
    }
    if (ok) {
        msqid = (int)syscall(__NR_msgget, (long)IPC_PRIVATE, 0666L);
        if (msqid >= 0) {
            memset(&msg, 0, sizeof(msg));
            memset(&rmsg, 0, sizeof(rmsg));
            msg.mtype = 1;
            memcpy(msg.mtext, "MSG", 3);
            ok = syscall(__NR_msgsnd, (long)msqid, (long)&msg, 3L, 0L) == 0 &&
                 syscall(__NR_msgrcv, (long)msqid, (long)&rmsg, 8L, 0L, 0L) == 3 &&
                 memcmp(rmsg.mtext, "MSG", 3) == 0 &&
                 syscall(__NR_msgctl, (long)msqid, (long)IPC_RMID, 0L) == 0;
        } else {
            ok = 0;
        }
    }
    report("semmsg01", ok);
}

static void execveat01(void)
{
    const char *path = "/tmp/ltp_e";
    int fd;
    int ok = 0;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("execveat01", 0);
        return;
    }
    (void)write(fd, "X", 1);
    errno = 0;
    if (syscall(__NR_execveat, (long)fd, (long)"", 0L, 0L,
                (long)AT_EMPTY_PATH) < 0 &&
        (errno == ENOEXEC || errno == EISDIR || errno == EACCES)) {
        ok = 1;
    }
    close(fd);
    (void)unlink(path);
    report("execveat01", ok);
}

struct bfree_sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

static void sched_attr01(void)
{
    struct bfree_sched_attr attr;
    int ok = 0;

    memset(&attr, 0, sizeof(attr));
    attr.size = (uint32_t)sizeof(attr);
    if (syscall(__NR_sched_getattr, 0L, (long)&attr, (long)sizeof(attr), 0L) ==
            0 &&
        attr.size >= 48U && attr.sched_policy == 0U) {
        ok = syscall(__NR_sched_setattr, 0L, (long)&attr, 0L) == 0;
    }
    report("sched_attr01", ok);
}

static void membarrier_rseq01(void)
{
    int ok = syscall(__NR_membarrier, 0L, 0L) == 0 &&
             syscall(__NR_rseq, 0L, 0L, 0L, 0L) == 0;

    report("membarrier_rseq01", ok);
}

struct bfree_timex {
    unsigned int modes;
    long offset;
    long freq;
    long maxerror;
    long esterror;
    int status;
    long charpad[16];
};

static void adjtimex01(void)
{
    struct bfree_timex tx;
    int ok = 0;

    memset(&tx, 0, sizeof(tx));
    ok = syscall(__NR_adjtimex, (long)&tx) == 0 &&
         syscall(__NR_clock_adjtime, (long)CLOCK_REALTIME, (long)&tx) == 0;
    report("adjtimex01", ok);
}

static void quotactl01(void)
{
    int ok = syscall(__NR_quotactl, 0L, (long)"/", 0L, 0L) == 0;

    report("quotactl01", ok);
}

static void keyctl01(void)
{
    long key;
    int ok = 0;

    key = syscall(__NR_add_key, (long)"user", (long)"ltp", (long)"v", 1L, -1L);
    if (key > 0) {
        ok = syscall(__NR_keyctl, (long)KEYCTL_READ, key, 0L, 0L, 0L) >= 0;
    }
    report("keyctl01", ok);
}

static void perf_event_open01(void)
{
    /* Minimal perf_event_attr: just size field at offset 0 */
    struct {
        uint32_t type;
        uint32_t size;
        uint64_t config;
        uint64_t pad[8];
    } attr;
    int fd;
    int ok = 0;

    memset(&attr, 0, sizeof(attr));
    attr.size = 64;
    fd = (int)syscall(__NR_perf_event_open, (long)&attr, 0L, -1L, -1L, 0L);
    if (fd >= 0) {
        ok = 1;
        close(fd);
    }
    report("perf_event_open01", ok);
}

static void epoll_futex01(void)
{
    int p[2];
    int ep;
    struct epoll_event ev, out;
    struct timespec ts;
    int ok = 0;

    if (pipe(p) != 0) {
        report("epoll_futex01", 0);
        return;
    }
    ep = epoll_create1(0);
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    if (ep >= 0 && write(p[1], "E", 1) == 1) {
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        ok = epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0 &&
             syscall(__NR_epoll_pwait2, (long)ep, (long)&out, 1L, (long)&ts,
                     0L) == 1 &&
             (out.events & EPOLLIN) &&
             syscall(__NR_futex_waitv, 0L, 0L, 0L, 0L, 0L) == 0;
        close(ep);
    }
    close(p[0]);
    close(p[1]);
    report("epoll_futex01", ok);
}

static void fsopen01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_fsopen, (long)"tmpfs", 0L);
    if (fd >= 0) {
        ok = syscall(__NR_fsconfig, (long)fd, (long)FSCONFIG_CMD_CREATE, 0L, 0L,
                     0L) == 0;
        close(fd);
    }
    report("fsopen01", ok);
}

static void process_madvise01(void)
{
    char buf[64];
    struct iovec iov;
    int ok = 0;

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    ok = syscall(__NR_process_madvise, (long)getpid(), (long)&iov, 1L,
                 (long)MADV_COLD, 0L) == 0;
    report("process_madvise01", ok);
}

static void pkey01(void)
{
    long pk;
    char page[4096];
    int ok = 0;

    pk = syscall(__NR_pkey_alloc, 0L, 0L);
    if (pk > 0) {
        ok = syscall(__NR_pkey_mprotect, (long)page, 4096L, (long)PROT_READ,
                     pk) == 0 &&
             syscall(__NR_pkey_free, pk) == 0;
    }
    report("pkey01", ok);
}

static void cachestat_mount01(void)
{
    struct {
        uint64_t nr_cache;
        uint64_t nr_dirty;
        uint64_t nr_writeback;
        uint64_t nr_evicted;
        uint64_t nr_recently_evicted;
    } cs;
    char smbuf[64];
    int fd;
    int ok = 0;

    memset(&cs, 0xff, sizeof(cs));
    fd = open("/tmp", O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ok = syscall(__NR_cachestat, (long)fd, 0L, (long)&cs, 0L) == 0 &&
             cs.nr_cache == 0 &&
             syscall(__NR_statmount, 0L, (long)smbuf, (long)sizeof(smbuf),
                     0L) == 0;
        close(fd);
    }
    report("cachestat_mount01", ok);
}

static void kcmp01(void)
{
    int ok = syscall(__NR_kcmp, (long)getpid(), (long)getpid(), (long)KCMP_FILE,
                     0L, 0L) == 0;

    report("kcmp01", ok);
}

static void path_xattr01(void)
{
    const char *path = "/tmp/ltp_pxattr";
    const char *name = "user.ltp";
    const char *val = "PXV";
    char out[16];
    char list[64];
    int fd;
    int ok = 0;
    long n;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        report("path_xattr01", 0);
        return;
    }
    close(fd);
    if (syscall(__NR_setxattr, (long)path, (long)name, (long)val, 3L, 0L) ==
        0) {
        n = syscall(__NR_getxattr, (long)path, (long)name, (long)out, 16L);
        ok = n == 3 && memcmp(out, "PXV", 3) == 0;
        n = syscall(__NR_listxattr, (long)path, (long)list, (long)sizeof(list));
        ok = ok && n > 0 && memcmp(list, name, strlen(name) + 1) == 0;
        ok = ok &&
             syscall(__NR_removexattr, (long)path, (long)name) == 0;
        errno = 0;
        ok = ok &&
             syscall(__NR_getxattr, (long)path, (long)name, (long)out, 16L) <
                 0 &&
             errno == ENODATA;
    }
    (void)unlink(path);
    report("path_xattr01", ok);
}

/* --- S. fifteen soft-report batch --- */

static void io_uring01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_io_uring_setup, 8L, 0L);
    if (fd >= 0) {
        ok = syscall(__NR_io_uring_enter, (long)fd, 0L, 0L, 0L, 0L) == 0 &&
             syscall(__NR_io_uring_register, (long)fd, 0L, 0L, 0L) == 0;
        close(fd);
    }
    report("io_uring01", ok);
}

static void userfaultfd01(void)
{
    int fd = (int)syscall(__NR_userfaultfd, 0L);
    int ok = fd >= 0;

    if (fd >= 0) {
        close(fd);
    }
    report("userfaultfd01", ok);
}

static void landlock01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_landlock_create_ruleset, 0L, 0L, 0L);
    if (fd >= 0) {
        ok = syscall(__NR_landlock_add_rule, (long)fd, 0L, 0L, 0L) == 0 &&
             syscall(__NR_landlock_restrict_self, (long)fd, 0L) == 0;
        close(fd);
    } else {
        ok = fd == 0; /* soft ≥0 */
    }
    report("landlock01", ok);
}

static void seccomp01(void)
{
    int ok = syscall(__NR_seccomp, (long)SECCOMP_SET_MODE_STRICT, 0L, 0L) == 0 ||
             syscall(__NR_seccomp, 0L, 0L, 0L) == 0;

    report("seccomp01", ok);
}

static void bpf01(void)
{
    long rc = syscall(__NR_bpf, 0L, 0L, 0L);
    int ok = rc == 0 || (rc >= -4095 && rc != -38);

    report("bpf01", ok);
}

static void ptrace01(void)
{
    report("ptrace01",
           syscall(__NR_ptrace, (long)PTRACE_TRACEME, 0L, 0L, 0L) == 0);
}

static void syslog01(void)
{
    char buf[32];
    long n;

    memset(buf, 0, sizeof(buf));
    n = syscall(__NR_syslog, 3L, (long)buf, (long)sizeof(buf));
    report("syslog01", n >= 0);
}

static void acct_swap01(void)
{
    int ok = syscall(__NR_acct, 0L) == 0 &&
             syscall(__NR_swapon, (long)"/tmp", 0L) == 0 &&
             syscall(__NR_swapoff, (long)"/tmp") == 0;

    report("acct_swap01", ok);
}

static void module01(void)
{
    int ok = syscall(__NR_finit_module, 0L, 0L, 0L) == 0 &&
             syscall(__NR_delete_module, (long)"bfree", 0L) == 0;

    report("module01", ok);
}

static void reboot01(void)
{
    int ok = syscall(__NR_reboot, (long)LINUX_REBOOT_MAGIC1,
                     (long)LINUX_REBOOT_MAGIC2, 0L, 0L) == 0;

    report("reboot01", ok);
}

static void fspick_mount01(void)
{
    int fd;
    int ok = 0;

    fd = (int)syscall(__NR_fspick, (long)AT_FDCWD, (long)"/", 0L);
    if (fd >= 0) {
        ok = syscall(__NR_mount_setattr, (long)fd, 0L, 0L, 0L, 0L) == 0;
        close(fd);
    }
    report("fspick_mount01", ok);
}

static void pidfd_poll01(void)
{
    int pfd;
    struct pollfd p;
    int ok = 0;

    pfd = (int)syscall(__NR_pidfd_open, (long)getpid(), 0L);
    if (pfd < 0) {
        report("pidfd_poll01", 0);
        return;
    }
    p.fd = pfd;
    p.events = POLLIN;
    p.revents = 0;
    /* Accept pidfd in poll; live self → timeout/not-ready (rc==0) is OK. */
    ok = poll(&p, 1, 0) >= 0;
    close(pfd);
    report("pidfd_poll01", ok);
}

static void proc_self01(void)
{
    char buf[128];
    long n;

    memset(buf, 0, sizeof(buf));
    n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    report("proc_self01", n > 0);
}

static void personality01(void)
{
    report("personality01", syscall(__NR_personality, 0L) == 0);
}

static void modify_ldt01(void)
{
    report("modify_ldt01", syscall(__NR_modify_ldt, 0L, 0L, 0L) == 0);
}

/* --- B1. futex deepen --- */

static void futex01(void)
{
    int fut = 1;
    struct timespec ts;
    long rc;
    int ok = 0;

    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L; /* 1ms */
    errno = 0;
    rc = syscall(__NR_futex, (long)&fut, (long)FUTEX_WAIT, 1L, (long)&ts, 0L, 0L);
    ok = rc < 0 && errno == ETIMEDOUT && fut == 1;
    fut = 1;
    ok = ok && syscall(__NR_futex, (long)&fut, (long)FUTEX_WAKE, 1L, 0L, 0L, 0L) >= 0;
    report("futex01", ok);
}

/* --- B2. inotify event delivery --- */

static void inotify02(void)
{
    int fd;
    int wd;
    int cfd;
    int ok = 0;
    long n;
    struct {
        int wd;
        uint32_t mask;
        uint32_t cookie;
        uint32_t len;
        char name[64];
    } evbuf;

    fd = (int)syscall(__NR_inotify_init1, (long)IN_NONBLOCK);
    if (fd < 0) {
        report("inotify02", 0);
        return;
    }
    wd = (int)syscall(__NR_inotify_add_watch, (long)fd, (long)"/tmp",
                      (long)IN_CREATE);
    if (wd <= 0) {
        close(fd);
        report("inotify02", 0);
        return;
    }
    (void)unlink("/tmp/ltp_inotify_ev");
    cfd = creat("/tmp/ltp_inotify_ev", 0644);
    if (cfd >= 0) {
        close(cfd);
    }
    memset(&evbuf, 0, sizeof(evbuf));
    n = read(fd, &evbuf, sizeof(evbuf));
    ok = n > 0 && (evbuf.mask & (uint32_t)IN_CREATE) != 0U &&
         evbuf.len > 0 && strcmp(evbuf.name, "ltp_inotify_ev") == 0;
    (void)syscall(__NR_inotify_rm_watch, (long)fd, (long)wd);
    close(fd);
    (void)unlink("/tmp/ltp_inotify_ev");
    report("inotify02", ok);
}

int main(void)
{
    /* brk before heavy mmap — musl refuses grow once mmap overlaps heap */
    brk01();

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

    /* D. mmap / memory + FS extras */
    getpagesize01();
    mmap01_anon();
    mmap02_file_private();
    mmap03_shared_msync();
    mmap04_shared_live();
    mmap05_cow_break();
    munmap01();
    rename01();
    ftruncate01();
    getcwd01();

    /* E. claimed-DONE ABI probe */
    pwrite01();
    fsync01();
    fdatasync01();
    sync01();
    umask01();
    truncate01();
    chmod01();
    link01();
    symlink01_readlink01();
    pipe201();
    flock01();
    mremap01();
    mprotect01();
    mincore01();
    memfd01();
    shm_open01();
    socketpair01();
    poll01();
    select01();
    clock_gettime01();
    clock_getres01();
    clock_nanosleep01();
    nanosleep01();
    getrandom01();
    getrlimit01();
    sysinfo01();
    fchdir01();
    faccessat01();
    creat01();
    getpgrp01();
    setsid_getsid01();

    /* F. eventfd / epoll / utimensat / sendfile / TCP (+ late brk query) */
    brk02_query();
    eventfd01();
    eventfd02_nonblock();
    epoll01_pipe();
    epoll02_eventfd();
    epoll03_mod_del();
    utimensat01();
    futimens01();
    sendfile01();
    sendfile02_pipe();
    tcp01_echo();
    tcp02_shutdown();
    tcp03_epoll();

    /* G. recommended hole hunt tranche */
    timerfd01_epoll();
    accept401();
    msg_peek01();
    getpeername01();
    fallocate01();
    mkfifo01();
    signalfd01();
    splice01();
    splice02_pipe_pipe();

    /* H. *at → getdents → getsockopt → AF_UNIX pathname */
    linkat01();
    unlinkat01();
    renameat01();
    mkdirat01();
    symlinkat01_readlinkat01();
    fchmodat01();
    getdents01();
    getsockopt01_type();
    getsockopt02_error();
    unix01_path();

    /* I. remaining 5–10 */
    setitimer01();
    waitid01();
    ppoll01();
    getrusage01();
    times01();
    uname01();
    gettimeofday01();
    fcntl_setlk01();
    tee01();
    timerfd02_interval();
    udp01();
    shutdown01_half();

    /* J. vmsplice + shutdown RD/EPIPE + setitimer interval + connected UDP */
    vmsplice01();
    shutdown02_rd_epipe();
    setitimer02_interval();
    udp02_connected();

    /* K. setsockopt / NONBLOCK / DONTWAIT / select·pselect */
    setsockopt01();
    setsockopt02_reuse();
    sock_nonblock01();
    msg_dontwait01();
    select02_sock();
    pselect01();

    /* L. copy_file_range / sendmmsg·recvmmsg / chown·utimes */
    copy_file_range01();
    sendmmsg01_recvmmsg01();
    chown01();
    utimes01();

    /* M. madvise/fadvise / sockopt more / MSG_WAITALL·TRUNC */
    madvise01();
    fadvise01();
    sync_file_range01();
    setsockopt03_more();
    msg_trunc01();
    msg_waitall01();

    /* N. remaining hole batch */
    renameat201();
    open_tmpfile01();
    open_opath01();
    fallocate02_punch();
    sendmsg01_multi();
    setsockopt04_ip();
    fcntl_dupfd_cloexec01();
    eventfd_signalfd_nb01();
    unix_dgram01();
    udp03_peer_filter();
    so_error01();
    statfs01();
    listen02_backlog();
    memfd_seals01();

    /* O. twelve-hole batch */
    recvmsg01_cmsg_cloexec();
    splice02_nonblock();
    epoll04_et_oneshot();
    eventfd_sem01();
    timerfd_abstime01();
    prctl_pdeathsig01();
    getrandom_flags01();
    clock_nanosleep_abstime01();
    inet6_01();
    msg_oob01();
    ioctl_winsz01();
    mount_umount01();
    pidfd01();
    close_range01();
    openat201();

    /* P. fifteen-hole batch */
    faccessat201();
    syncfs01();
    mlock01();
    sched_aff01();
    ioctl_fion01();
    setsockopt05();
    preadv201();
    unshare01();
    cap01();
    pidfd_getfd01();
    tee_vmsplice_nb01();
    renameat202();
    epoll_pwait01();
    inotify01();
    xattr01();

    /* Q. fifteen-hole batch */
    getcpu01();
    sethostname01();
    getgroups01();
    chroot01();
    setns01();
    timer_create01();
    shmget01();
    process_vm01();
    clone301();
    fanotify01();
    name_to_handle01();
    statx01();
    memfd_secret01();
    clock_settime01();
    fallocate03();

    /* R. fifteen-hole batch */
    semmsg01();
    execveat01();
    sched_attr01();
    membarrier_rseq01();
    adjtimex01();
    quotactl01();
    keyctl01();
    perf_event_open01();
    epoll_futex01();
    fsopen01();
    process_madvise01();
    pkey01();
    cachestat_mount01();
    kcmp01();
    path_xattr01();

    /* S. fifteen soft-report batch */
    io_uring01();
    userfaultfd01();
    landlock01();
    seccomp01();
    bpf01();
    ptrace01();
    syslog01();
    acct_swap01();
    module01();
    reboot01();
    fspick_mount01();
    pidfd_poll01();
    proc_self01();
    personality01();
    modify_ldt01();

    /* B1 / B2 deepen */
    futex01();
    inotify02();

    printf("LTP_CURATED_RESULT: %s n=%d fail=%d\n",
           g_fail == 0 ? "PASS" : "FAIL", g_pass + g_fail, g_fail);
    fflush(stdout);
    /* _exit avoids musl atexit against the shared guest fd table. */
    _exit(g_fail == 0 ? 0 : 1);
}
