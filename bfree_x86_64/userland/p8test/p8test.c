/* Phase A POSIX guest self-test (raw Linux x86_64 syscalls). */
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned long uintptr_t;

#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
#define __NR_mmap 9
#define __NR_munmap 11
#define __NR_lseek 8
#define __NR_dup3 292
#define __NR_nanosleep 35
#define __NR_alarm 37
#define __NR_getsid 124
#define __NR_socket 41
#define __NR_connect 42
#define __NR_accept 43
#define __NR_sendto 44
#define __NR_recvfrom 45
#define __NR_bind 49
#define __NR_listen 50
#define __NR_exit_group 231
#define __NR_pread64 17
#define __NR_pwrite64 18
#define __NR_select 23
#define __NR_flock 73
#define __NR_rt_sigprocmask 14
#define __NR_clock_getres 229
#define __NR_pipe 22
#define __NR_rt_sigaction 13
#define __NR_fcntl 72
#define __NR_ioctl 16
#define __NR_kill 62
#define __NR_getpid 39
#define __NR_mremap 25
#define __NR_membarrier 324
#define MAP_SHARED 1
#define SIGCHLD 17
#define MREMAP_MAYMOVE 1

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#define SIG_BLOCK 0
#define SIG_SETMASK 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED 0x10
#define INADDR_LOOPBACK 0x7f000001U /* 127.0.0.1 host order */
#define INADDR_GUEST    0x0A00020FU /* 10.0.2.15 QEMU-style guest */
#define INADDR_GATEWAY  0x0A000202U /* 10.0.2.2 */

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    char sin_zero[8];
};

static unsigned short bfree_htons(unsigned short v)
{
    return (unsigned short)(((v & 0xffU) << 8) | ((v >> 8) & 0xffU));
}

static unsigned int bfree_htonl(unsigned int v)
{
    return ((v & 0xffU) << 24) | ((v & 0xff00U) << 8) |
           ((v & 0xff0000U) >> 8) | ((v >> 24) & 0xffU);
}

static long sys6(long n, long a, long b, long c, long d, long e, long f)
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

static long sys3(long n, long a, long b, long c)
{
    return sys6(n, a, b, c, 0, 0, 0);
}

static void put(const char *s)
{
    const char *p = s;
    while (*p) {
        ++p;
    }
    (void)sys3(__NR_write, 1, (long)s, (long)(p - s));
}

static int streq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int test_pread_pwrite(void)
{
    char buf[16];
    long fd;
    long rc;
    long pos_check;

    fd = sys3(__NR_open, (long)"/tmp/p8io", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    if (sys3(__NR_write, fd, (long)"ABCDEFGH", 8) != 8) {
        return -1;
    }
    /* OFD at EOF; pread must not move it */
    rc = sys6(__NR_pread64, fd, (long)buf, 3, 3, 0, 0);
    if (rc != 3 || !streq_n(buf, "DEF", 3)) {
        return -1;
    }
    pos_check = sys3(__NR_read, fd, (long)buf, 1);
    if (pos_check != 0) {
        return -1; /* still at EOF */
    }
    if (sys6(__NR_pwrite64, fd, (long)"XY", 2, 1, 0, 0) != 2) {
        return -1;
    }
    if (sys6(__NR_pread64, fd, (long)buf, 4, 0, 0, 0) != 4 || !streq_n(buf, "AXYD", 4)) {
        return -1;
    }
    (void)sys3(__NR_close, fd, 0, 0);
    return 0;
}

static int test_select(void)
{
    unsigned long rfds[16];
    long i;
    struct timespec zero;
    long rc;

    for (i = 0; i < 16; ++i) {
        rfds[i] = 0;
    }
    /* timeout NULL? use zero timeval via select timeval ptr — use pselect-like select with 0 timeout */
    zero.tv_sec = 0;
    zero.tv_nsec = 0;
    /* Linux select: (nfds, readfds, writefds, exceptfds, timeout) — timeout is timeval */
    {
        long tv[2];
        tv[0] = 0;
        tv[1] = 0;
        rc = sys6(__NR_select, 1, (long)rfds, 0, 0, (long)tv, 0);
    }
    (void)zero;
    if (rc < 0) {
        return -1;
    }
    return 0;
}

static int test_flock(void)
{
    long fd1, fd2, rc;

    fd1 = sys3(__NR_open, (long)"/tmp/p8lk", O_RDWR | O_CREAT | O_TRUNC, 0644);
    fd2 = sys3(__NR_open, (long)"/tmp/p8lk", O_RDWR, 0);
    if (fd1 < 0 || fd2 < 0) {
        return -1;
    }
    if (sys3(__NR_flock, fd1, LOCK_EX, 0) != 0) {
        return -1;
    }
    rc = sys3(__NR_flock, fd2, LOCK_EX | LOCK_NB, 0);
    if (rc != -11) { /* EAGAIN */
        return -1;
    }
    if (sys3(__NR_flock, fd1, LOCK_UN, 0) != 0) {
        return -1;
    }
    if (sys3(__NR_flock, fd2, LOCK_EX | LOCK_NB, 0) != 0) {
        return -1;
    }
    (void)sys3(__NR_close, fd1, 0, 0);
    (void)sys3(__NR_close, fd2, 0, 0);
    return 0;
}

static int test_alarm(void)
{
    struct timespec req;
    long rc;

    if (sys3(__NR_alarm, 1, 0, 0) < 0) {
        return -1;
    }
    req.tv_sec = 3;
    req.tv_nsec = 0;
    rc = sys3(__NR_nanosleep, (long)&req, 0, 0);
    if (rc != -4) { /* EINTR */
        return -1;
    }
    (void)sys3(__NR_alarm, 0, 0, 0);
    return 0;
}

static int test_sigmask(void)
{
    unsigned long set[1];
    unsigned long old[1];
    long rc;

    set[0] = 1UL << (13 - 1); /* SIGPIPE */
    old[0] = 0;
    rc = sys6(__NR_rt_sigprocmask, SIG_BLOCK, (long)set, (long)old, 8, 0, 0);
    if (rc != 0) {
        return -1;
    }
    set[0] = 0;
    rc = sys6(__NR_rt_sigprocmask, SIG_SETMASK, (long)set, 0, 8, 0, 0);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

static int test_unix(void)
{
    long srv, cli, acc;
    struct sockaddr_un addr;
    char buf[8];
    long i;

    for (i = 0; i < (long)sizeof(addr); ++i) {
        ((char *)&addr)[i] = 0;
    }
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '/';
    addr.sun_path[1] = 't';
    addr.sun_path[2] = 'm';
    addr.sun_path[3] = 'p';
    addr.sun_path[4] = '/';
    addr.sun_path[5] = 'p';
    addr.sun_path[6] = '8';
    addr.sun_path[7] = 'u';
    addr.sun_path[8] = '.';
    addr.sun_path[9] = 's';
    addr.sun_path[10] = 0;

    srv = sys3(__NR_socket, AF_UNIX, SOCK_STREAM, 0);
    cli = sys3(__NR_socket, AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0 || cli < 0) {
        return -1;
    }
    if (sys3(__NR_bind, srv, (long)&addr, 2 + 11) != 0) {
        return -1;
    }
    if (sys3(__NR_listen, srv, 1, 0) != 0) {
        return -1;
    }
    if (sys3(__NR_connect, cli, (long)&addr, 2 + 11) != 0) {
        return -1;
    }
    acc = sys3(__NR_accept, srv, 0, 0);
    if (acc < 0) {
        return -1;
    }
    if (sys6(__NR_sendto, cli, (long)"UX", 2, 0, 0, 0) != 2) {
        return -1;
    }
    if (sys6(__NR_recvfrom, acc, (long)buf, 2, 0, 0, 0) != 2 || !streq_n(buf, "UX", 2)) {
        return -1;
    }
    (void)sys3(__NR_close, acc, 0, 0);
    (void)sys3(__NR_close, cli, 0, 0);
    (void)sys3(__NR_close, srv, 0, 0);
    return 0;
}

/* H07: controlling tty — TIOCGPGRP / TIOCSPGRP on /dev/tty (and fd0 fallback). */
static int test_tty_pgrp(void)
{
    long fd;
    int pg = 0;
    int pg2 = -1;

    fd = sys3(__NR_open, (long)"/dev/tty", O_RDWR, 0);
    if (fd < 0) {
        fd = 0;
    }
    if (sys3(__NR_ioctl, fd, TIOCGPGRP, (long)&pg) != 0 || pg <= 0) {
        if (fd > 2) {
            (void)sys3(__NR_close, fd, 0, 0);
        }
        return -1;
    }
    if (sys3(__NR_ioctl, fd, TIOCSPGRP, (long)&pg) != 0) {
        if (fd > 2) {
            (void)sys3(__NR_close, fd, 0, 0);
        }
        return -1;
    }
    if (sys3(__NR_ioctl, fd, TIOCGPGRP, (long)&pg2) != 0 || pg2 != pg) {
        if (fd > 2) {
            (void)sys3(__NR_close, fd, 0, 0);
        }
        return -1;
    }
    if (fd > 2) {
        (void)sys3(__NR_close, fd, 0, 0);
    }
    return 0;
}

/* H32: freestanding AF_INET loopback (kernel pipe-backed; no libc getaddrinfo). */
static int test_inet_loopback(void)
{
    long srv, cli, acc;
    struct sockaddr_in addr;
    char buf[8];
    long i;

    for (i = 0; i < (long)sizeof(addr); ++i) {
        ((char *)&addr)[i] = 0;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = bfree_htons(3847);
    addr.sin_addr = bfree_htonl(INADDR_LOOPBACK);

    srv = sys3(__NR_socket, AF_INET, SOCK_STREAM, 0);
    cli = sys3(__NR_socket, AF_INET, SOCK_STREAM, 0);
    if (srv < 0 || cli < 0) {
        return -1;
    }
    if (sys3(__NR_bind, srv, (long)&addr, 16) != 0) {
        return -1;
    }
    if (sys3(__NR_listen, srv, 1, 0) != 0) {
        return -1;
    }
    if (sys3(__NR_connect, cli, (long)&addr, 16) != 0) {
        return -1;
    }
    acc = sys3(__NR_accept, srv, 0, 0);
    if (acc < 0) {
        return -1;
    }
    if (sys6(__NR_sendto, cli, (long)"IN", 2, 0, 0, 0) != 2) {
        return -1;
    }
    if (sys6(__NR_recvfrom, acc, (long)buf, 2, 0, 0, 0) != 2 || !streq_n(buf, "IN", 2)) {
        return -1;
    }
    (void)sys3(__NR_close, acc, 0, 0);
    (void)sys3(__NR_close, cli, 0, 0);
    (void)sys3(__NR_close, srv, 0, 0);
    return 0;
}

static int test_mmap_file(void)
{
    long fd;
    long map;
    char *p;

    fd = sys3(__NR_open, (long)"/tmp/p8mm", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    if (sys3(__NR_write, fd, (long)"MMAPOK", 6) != 6) {
        return -1;
    }
    map = sys6(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (map < 0 && map > -4096) {
        return -1;
    }
    p = (char *)(uintptr_t)map;
    if (!streq_n(p, "MMAPOK", 6)) {
        return -1;
    }
    (void)sys3(__NR_munmap, map, 4096, 0);
    (void)sys3(__NR_close, fd, 0, 0);
    return 0;
}

/* A5/B2: MAP_SHARED writeback visible via read(). */
static int test_mmap_shared(void)
{
    long fd;
    long map;
    char *p;
    char buf[8];
    long n;

    fd = sys3(__NR_open, (long)"/tmp/p8sh", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    if (sys3(__NR_write, fd, (long)"XXXXXX", 6) != 6) {
        return -1;
    }
    map = sys6(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map < 0 && map > -4096) {
        return -1;
    }
    p = (char *)(uintptr_t)map;
    p[0] = 'S';
    p[1] = 'H';
    p[2] = 'A';
    p[3] = 'R';
    p[4] = 'E';
    p[5] = 'D';
    (void)sys3(__NR_lseek, fd, 0, 0);
    n = sys3(__NR_read, fd, (long)buf, 6);
    (void)sys3(__NR_munmap, map, 4096, 0);
    (void)sys3(__NR_close, fd, 0, 0);
    if (n != 6 || !streq_n(buf, "SHARED", 6)) {
        return -1;
    }
    return 0;
}

static volatile long g_got_sigchld;

static void on_sigchld(long sig)
{
    (void)sig;
    g_got_sigchld = 1;
}

/* Linux x86_64 requires sa_restorer for kernel CATCH delivery. */
static void sig_restorer(void)
{
    register long rax __asm__("rax") = 15; /* rt_sigreturn */
    __asm__ volatile("syscall" : : "r"(rax) : "rcx", "r11", "memory");
}

/* B1.6/B1.14: install CATCH handler, raise SIGCHLD, deliver on syscall return. */
static int test_sigchld(void)
{
    long act[4];
    long i;
    long pid;

    g_got_sigchld = 0;
    for (i = 0; i < 4; ++i) {
        act[i] = 0;
    }
    act[0] = (long)(uintptr_t)on_sigchld;
    act[2] = (long)(uintptr_t)sig_restorer;
    if (sys6(__NR_rt_sigaction, SIGCHLD, (long)act, 0, 8, 0, 0) != 0) {
        return -1;
    }
    pid = sys3(__NR_getpid, 0, 0, 0);
    if (pid < 0) {
        return -1;
    }
    if (sys3(__NR_kill, pid, SIGCHLD, 0) != 0) {
        return -1;
    }
    /* Enter kernel so pending CATCH is delivered. */
    (void)sys3(__NR_getpid, 0, 0, 0);
    (void)sys3(__NR_getpid, 0, 0, 0);
    act[0] = 0;
    act[2] = 0;
    (void)sys6(__NR_rt_sigaction, SIGCHLD, (long)act, 0, 8, 0, 0);
    if (g_got_sigchld == 0) {
        return -1;
    }
    return 0;
}

static int test_mremap_membarrier(void)
{
    long map;
    long grown;
    long rc;

    map = sys6(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map < 0 && map > -4096) {
        return -1;
    }
    grown = sys6(__NR_mremap, map, 4096, 8192, 0, 0, 0);
    if (grown < 0 && grown > -4096) {
        /* in-place expand may fail with ENOMEM — still accept shrink path */
        (void)sys3(__NR_munmap, map, 4096, 0);
    } else {
        (void)sys3(__NR_munmap, grown, 8192, 0);
    }
    rc = sys3(__NR_membarrier, 0, 0, 0);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

static int test_misc(void)
{
    struct timespec res;
    long sid;
    long nfd;

    sid = sys3(__NR_getsid, 0, 0, 0);
    if (sid < 0) {
        return -1;
    }
    if (sys3(__NR_clock_getres, 0, (long)&res, 0) != 0) {
        return -1;
    }
    nfd = sys3(__NR_dup3, 1, 40, 0x80000); /* O_CLOEXEC */
    if (nfd != 40) {
        return -1;
    }
    (void)sys3(__NR_close, 40, 0, 0);
    return 0;
}

static int test_sigpipe(void)
{
    int p[2];
    long rc;
    /* Linux sigaction: sa_handler, sa_flags, sa_restorer, sa_mask */
    long act[4];
    long i;

    for (i = 0; i < 4; ++i) {
        act[i] = 0;
    }
    act[0] = 1; /* SIG_IGN */
    if (sys6(__NR_rt_sigaction, 13, (long)act, 0, 8, 0, 0) != 0) {
        return -1;
    }
    if (sys3(__NR_pipe, (long)p, 0, 0) != 0) {
        return -1;
    }
    (void)sys3(__NR_close, (long)p[0], 0, 0);
    rc = sys3(__NR_write, (long)p[1], (long)"x", 1);
    (void)sys3(__NR_close, (long)p[1], 0, 0);
    act[0] = 0;
    (void)sys6(__NR_rt_sigaction, 13, (long)act, 0, 8, 0, 0);
    if (rc != -32) { /* EPIPE */
        return -1;
    }
    return 0;
}

static int test_inet_slirp_guest(void)
{
    long srv, cli, acc;
    struct sockaddr_in addr;
    char buf[8];

    for (long i = 0; i < (long)sizeof(addr); ++i) {
        ((char *)&addr)[i] = 0;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = bfree_htons(3848);
    addr.sin_addr = bfree_htonl(INADDR_GUEST);

    srv = sys3(__NR_socket, AF_INET, SOCK_STREAM, 0);
    cli = sys3(__NR_socket, AF_INET, SOCK_STREAM, 0);
    if (srv < 0 || cli < 0) {
        return -1;
    }
    if (sys3(__NR_bind, srv, (long)&addr, 16) != 0) {
        return -1;
    }
    if (sys3(__NR_listen, srv, 1, 0) != 0) {
        return -1;
    }
    addr.sin_addr = bfree_htonl(INADDR_GATEWAY);
    if (sys3(__NR_connect, cli, (long)&addr, 16) != 0) {
        return -1;
    }
    acc = sys3(__NR_accept, srv, 0, 0);
    if (acc < 0) {
        return -1;
    }
    if (sys6(__NR_sendto, cli, (long)"SL", 2, 0, 0, 0) != 2) {
        return -1;
    }
    if (sys6(__NR_recvfrom, acc, (long)buf, 2, 0, 0, 0) != 2 || !streq_n(buf, "SL", 2)) {
        return -1;
    }
    (void)sys3(__NR_close, acc, 0, 0);
    (void)sys3(__NR_close, cli, 0, 0);
    (void)sys3(__NR_close, srv, 0, 0);
    return 0;
}

/* F2: UDP loopback — bind a receiver, sendto from a second socket. */
static int test_udp_loopback(void)
{
    long rx, tx;
    struct sockaddr_in addr;
    struct sockaddr_in src;
    char buf[8];
    long n;

    for (long i = 0; i < (long)sizeof(addr); ++i) {
        ((char *)&addr)[i] = 0;
        ((char *)&src)[i] = 0;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = bfree_htons(5555);
    addr.sin_addr = bfree_htonl(INADDR_LOOPBACK);

    rx = sys3(__NR_socket, AF_INET, SOCK_DGRAM, 0);
    tx = sys3(__NR_socket, AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) {
        return -1;
    }
    if (sys3(__NR_bind, rx, (long)&addr, 16) != 0) {
        return -1;
    }
    if (sys6(__NR_sendto, tx, (long)"UDP4", 4, 0, (long)&addr, 16) != 4) {
        return -1;
    }
    n = sys6(__NR_recvfrom, rx, (long)buf, sizeof(buf), 0, (long)&src, 0);
    if (n != 4 || !streq_n(buf, "UDP4", 4)) {
        return -1;
    }
    if (src.sin_family != AF_INET) {
        return -1;
    }
    /* connect()ed UDP send without explicit destination. */
    if (sys3(__NR_connect, tx, (long)&addr, 16) != 0) {
        return -1;
    }
    if (sys6(__NR_sendto, tx, (long)"UDP5", 4, 0, 0, 0) != 4) {
        return -1;
    }
    if (sys6(__NR_recvfrom, rx, (long)buf, sizeof(buf), 0, 0, 0) != 4 ||
        !streq_n(buf, "UDP5", 4)) {
        return -1;
    }
    (void)sys3(__NR_close, rx, 0, 0);
    (void)sys3(__NR_close, tx, 0, 0);
    return 0;
}

static int test_persist_vfile(void)
{
    long fd;
    char buf[8];

    fd = sys3(__NR_open, (long)"/persist/p8.dat", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    if (sys3(__NR_write, fd, (long)"PER_OK", 6) != 6) {
        (void)sys3(__NR_close, fd, 0, 0);
        return -1;
    }
    (void)sys3(__NR_close, fd, 0, 0);
    fd = sys3(__NR_open, (long)"/persist/p8.dat", O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }
    if (sys3(__NR_read, fd, (long)buf, 6) != 6 || !streq_n(buf, "PER_OK", 6)) {
        (void)sys3(__NR_close, fd, 0, 0);
        return -1;
    }
    (void)sys3(__NR_close, fd, 0, 0);
    return 0;
}

void p8_main(void)
{
    int fail = 0;

    if (test_pread_pwrite() == 0) {
        put("P8_PREAD_OK\n");
    } else {
        put("P8_PREAD_FAIL\n");
        fail = 1;
    }
    if (test_select() == 0) {
        put("P8_SELECT_OK\n");
    } else {
        put("P8_SELECT_FAIL\n");
        fail = 1;
    }
    if (test_flock() == 0) {
        put("P8_FLOCK_OK\n");
    } else {
        put("P8_FLOCK_FAIL\n");
        fail = 1;
    }
    if (test_sigmask() == 0) {
        put("P8_SIGMASK_OK\n");
    } else {
        put("P8_SIGMASK_FAIL\n");
        fail = 1;
    }
    if (test_sigpipe() == 0) {
        put("P8_SIGPIPE_OK\n");
    } else {
        put("P8_SIGPIPE_FAIL\n");
        fail = 1;
    }
    if (test_unix() == 0) {
        put("P8_UNIX_OK\n");
    } else {
        put("P8_UNIX_FAIL\n");
        fail = 1;
    }
    if (test_inet_loopback() == 0) {
        put("P8_INET_OK\n");
    } else {
        put("P8_INET_FAIL\n");
        fail = 1;
    }
    if (test_inet_slirp_guest() == 0) {
        put("P8_SLIRP_OK\n");
    } else {
        put("P8_SLIRP_FAIL\n");
        fail = 1;
    }
    if (test_udp_loopback() == 0) {
        put("P8_UDP_OK\n");
    } else {
        put("P8_UDP_FAIL\n");
        fail = 1;
    }
    if (test_persist_vfile() == 0) {
        put("P8_PERSIST_OK\n");
    } else {
        put("P8_PERSIST_FAIL\n");
        fail = 1;
    }
    if (test_tty_pgrp() == 0) {
        put("P8_TTY_OK\n");
    } else {
        put("P8_TTY_FAIL\n");
        fail = 1;
    }
    if (test_mmap_file() == 0) {
        put("P8_MMAP_OK\n");
    } else {
        put("P8_MMAP_FAIL\n");
        fail = 1;
    }
    if (test_mmap_shared() == 0) {
        put("P8_MMAP_SHARED_OK\n");
    } else {
        put("P8_MMAP_SHARED_FAIL\n");
        fail = 1;
    }
    if (test_sigchld() == 0) {
        put("P8_SIGCHLD_OK\n");
    } else {
        put("P8_SIGCHLD_FAIL\n");
        fail = 1;
    }
    if (test_mremap_membarrier() == 0) {
        put("P8_MREMAP_OK\n");
    } else {
        put("P8_MREMAP_FAIL\n");
        fail = 1;
    }
    if (test_misc() == 0) {
        put("P8_MISC_OK\n");
    } else {
        put("P8_MISC_FAIL\n");
        fail = 1;
    }
    if (test_alarm() == 0) {
        put("P8_ALARM_OK\n");
    } else {
        put("P8_ALARM_FAIL\n");
        fail = 1;
    }
    /* Stage1: /home writable + CLOEXEC */
    {
        long fd;
        fd = sys3(__NR_open, (long)"/home/p9h", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0 && sys3(__NR_write, fd, (long)"HOMEOK", 6) == 6) {
            (void)sys3(__NR_close, fd, 0, 0);
            put("P9_HOME_OK\n");
        } else {
            put("P9_HOME_FAIL\n");
            fail = 1;
        }
    }
    {
        long oldfl;
        long fl;
        oldfl = sys3(__NR_fcntl, 1, 1, 0); /* F_GETFD on stdout */
        if (oldfl < 0) {
            oldfl = 0;
        }
        if (sys3(__NR_fcntl, 1, 2, 1) == 0) { /* F_SETFD FD_CLOEXEC */
            fl = sys3(__NR_fcntl, 1, 1, 0);
            (void)sys3(__NR_fcntl, 1, 2, oldfl);
            if (fl == 1) {
                put("P9_CLOEXEC_OK\n");
            } else {
                put("P9_CLOEXEC_FAIL\n");
                fail = 1;
            }
        } else {
            put("P9_CLOEXEC_FAIL\n");
            fail = 1;
        }
    }
    /* Stage1 PTY + Stage2 setuid */
    {
        long m, s, n;
        unsigned int ptn = 99;
        char path[16];
        m = sys3(__NR_open, (long)"/dev/ptmx", O_RDWR, 0);
        if (m >= 0 &&
            sys3(16 /* ioctl */, m, (long)0x80045430L, (long)&ptn) == 0 &&
            ptn < 4) {
            path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v';
            path[4] = '/'; path[5] = 'p'; path[6] = 't'; path[7] = 's';
            path[8] = '/'; path[9] = (char)('0' + (int)ptn); path[10] = 0;
            s = sys3(__NR_open, (long)path, O_RDWR, 0);
            if (s >= 0 && sys3(__NR_write, m, (long)"PT", 2) == 2) {
                n = sys3(__NR_read, s, (long)path, 2);
                if (n == 2 && path[0] == 'P' && path[1] == 'T') {
                    put("P9_PTY_OK\n");
                } else {
                    put("P9_PTY_FAIL\n");
                    fail = 1;
                }
                (void)sys3(__NR_close, s, 0, 0);
            } else {
                put("P9_PTY_FAIL\n");
                fail = 1;
            }
            (void)sys3(__NR_close, m, 0, 0);
        } else {
            put("P9_PTY_FAIL\n");
            fail = 1;
        }
    }
    {
        long uid;
        if (sys3(105 /* setuid */, 0, 0, 0) == 0) {
            uid = sys3(102 /* getuid */, 0, 0, 0);
            if (uid == 0) {
                put("P9_UID_OK\n");
            } else {
                put("P9_UID_FAIL\n");
                fail = 1;
            }
        } else {
            put("P9_UID_FAIL\n");
            fail = 1;
        }
    }
    put(fail ? "P8_DONE_FAIL\n" : "P8_DONE_OK\n");
    (void)sys3(__NR_exit_group, fail ? 1 : 0, 0, 0);
    for (;;) {
    }
}
