/* Curated LTP-style POSIX subset for the B-Free guest (F3 / SF-04).
 *
 * Small, self-contained testcases named after their LTP counterparts.
 * Output format follows LTP: "TPASS: <name>" / "TFAIL: <name>", then a
 * summary line the gate greps:  LTP_CURATED_RESULT: PASS n=<N> fail=<F>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

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

/* open01: O_CREAT creates a file that can be reopened. */
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

/* write01/read01: write then read back verifies content. */
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

/* lseek01: SEEK_END returns file length. */
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

/* dup01: dup() gives a working descriptor. */
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

/* unlink01: unlinked name no longer opens. */
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

/* pipe01: pipe transfers bytes. */
static void pipe01(void)
{
    int p[2];
    char c = 0;
    int ok = pipe(p) == 0 &&
             write(p[1], "Z", 1) == 1 &&
             read(p[0], &c, 1) == 1 && c == 'Z';

    report("pipe01", ok);
    close(p[0]);
    close(p[1]);
}

/* getpid01: pid is positive and stable. */
static void getpid01(void)
{
    pid_t a = getpid();

    report("getpid01", a > 0 && a == getpid());
}

/* mkdir01/chdir01: create a directory and enter it. */
static void mkdir01_chdir01(void)
{
    int mk = mkdir("/tmp/ltp_dir01", 0755);
    int ok = (mk == 0 || errno == EEXIST);

    report("mkdir01", ok);
    report("chdir01", chdir("/tmp/ltp_dir01") == 0 && chdir("/") == 0);
}

/* stat01: size matches what write01 stored. */
static void stat01(void)
{
    struct stat st;
    int ok = stat("/tmp/ltp_rw01", &st) == 0 && st.st_size == 7;

    report("stat01", ok);
}

/* access01: F_OK on existing file, failure on missing one. */
static void access01(void)
{
    int ok = access("/tmp/ltp_rw01", F_OK) == 0 &&
             access("/tmp/ltp_no_such", F_OK) != 0;

    report("access01", ok);
}

/* fcntl01: F_GETFD works on an open descriptor. */
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

/* getuid01: uid syscalls return without error. */
static void getuid01(void)
{
    report("getuid01", getuid() != (uid_t)-1 && geteuid() != (uid_t)-1);
}

/* time01: CLOCK_MONOTONIC does not go backwards. */
static void time01(void)
{
    struct timespec a, b;
    int ok = clock_gettime(CLOCK_MONOTONIC, &a) == 0 &&
             clock_gettime(CLOCK_MONOTONIC, &b) == 0 &&
             (b.tv_sec > a.tv_sec ||
              (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));

    report("time01", ok);
}

int main(void)
{
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

    printf("LTP_CURATED_RESULT: %s n=%d fail=%d\n",
           g_fail == 0 ? "PASS" : "FAIL", g_pass + g_fail, g_fail);
    fflush(stdout);
    /* _exit avoids musl atexit against the shared guest fd table. */
    _exit(g_fail == 0 ? 0 : 1);
}
