/*
 * Guest desktop.elf link helpers for -nostdlib + Qt built with glibc-style names.
 * Linked after Qt archives; requires musl libc.a + libstdc++.a on the link line.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <pthread.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <unwind.h>

#include "../userland/desktop_qt/guest_serial.h"
#include "../userland/desktop_qt/guest_resource_holder_va.h"

extern "C" int __real_vfprintf(FILE *stream, const char *fmt, va_list ap);
extern "C" int __real_vprintf(const char *fmt, va_list ap);

static int bfree_guest_serial_vprintf(const char *fmt, va_list ap)
{
    char buf[768];
    va_list ap2;
    int n;

    if (!fmt)
        return -1;
    va_copy(ap2, ap);
    n = vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);
    if (n <= 0)
        return n;
    if ((size_t)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n] = '\0';
    bfree_guest_serial(buf);
    return n;
}

static int bfree_guest_stdio_to_serial(FILE *stream)
{
    if (!stream)
        return 1;
    return stream == stdout || stream == stderr;
}

extern "C" int __wrap_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    if (bfree_guest_stdio_to_serial(stream))
        return bfree_guest_serial_vprintf(fmt, ap);
    return __real_vfprintf(stream, fmt, ap);
}

extern "C" int __wrap_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = __wrap_vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" int __wrap_vprintf(const char *fmt, va_list ap)
{
    return __wrap_vfprintf(stdout, fmt, ap);
}

extern "C" int __wrap_printf(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = __wrap_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/* x86_64-elf-ld guest link has no gcc crtbegin.o — Qt static init needs this. */
extern "C" {
void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;
}

/* glibc ctype locale tables (Qt / glibc-built archives may reference these). */
static unsigned short int bfree_ctype_b_table[384];
static int bfree_ctype_b_table_init;

static void bfree_init_ctype_tables(void)
{
    int i;
    if (bfree_ctype_b_table_init)
        return;
    for (i = 0; i < 384; i++) {
        unsigned char c = (unsigned char)(i - 128);
        unsigned short f = 0;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            f |= 0x400; /* _ISalpha */
        if ((c >= '0' && c <= '9'))
            f |= 0x800; /* _ISdigit */
        if (c > 0x20 && c < 0x7f)
            f |= 0x4000; /* _ISprint */
        bfree_ctype_b_table[i] = f;
    }
    bfree_ctype_b_table_init = 1;
}

extern "C" const unsigned short int **__ctype_b_loc(void)
{
    static const unsigned short int *p;
    bfree_init_ctype_tables();
    if (!p)
        p = bfree_ctype_b_table + 128;
    return &p;
}

static int bfree_tolower_table[384];
static int bfree_toupper_table[384];

extern "C" const int32_t **__ctype_tolower_loc(void)
{
    static const int32_t *p;
    int i;
    bfree_init_ctype_tables();
    if (!p) {
        for (i = 0; i < 384; i++) {
            unsigned char c = (unsigned char)(i - 128);
            bfree_tolower_table[i] = (c >= 'A' && c <= 'Z') ? (int)c + 32 : (int)c;
        }
        p = (const int32_t *)(bfree_tolower_table + 128);
    }
    return &p;
}

extern "C" const int32_t **__ctype_toupper_loc(void)
{
    static const int32_t *p;
    int i;
    bfree_init_ctype_tables();
    if (!p) {
        for (i = 0; i < 384; i++) {
            unsigned char c = (unsigned char)(i - 128);
            bfree_toupper_table[i] = (c >= 'a' && c <= 'z') ? (int)c - 32 : (int)c;
        }
        p = (const int32_t *)(bfree_toupper_table + 128);
    }
    return &p;
}

extern "C" void bfree_guest_serial_hex_u64(uint64_t v);

static volatile int bfree_guest_qv4_mmap_active;
static unsigned g_qv4_trace_tag_n;

static void bfree_guest_qv4_trace_tag(const char *tag)
{
    if (!bfree_guest_qv4_mmap_active || !tag || g_qv4_trace_tag_n >= 512u)
        return;
    ++g_qv4_trace_tag_n;
    bfree_guest_serial_lit("[qv4] ");
    bfree_guest_serial_lit(tag);
    bfree_guest_serial_lit("\n");
}

static void bfree_guest_qv4_trace_malloc(size_t n)
{
    (void)n;
    /* No serial from malloc — reentrancy during QQmlEngine/QV4 #GP in .rodata. */
}

/* LFS64 names Qt may reference when guest Qt was configured with linux-g++ / glibc headers. */
extern "C" int __real_open(const char *path, int flags, ...);

#define BFREE_GUEST_OPEN_FB0_FD     0x2000
#define BFREE_GUEST_OPEN_INPUT_FD   0x3000
#define BFREE_GUEST_OPEN_NULL_FD    0x3700
#define BFREE_GUEST_OPEN_URANDOM_FD 0x3701

static int bfree_guest_open_virtual_fd(const char *path)
{
    if (!path) {
        return -1;
    }
    if (strcmp(path, "/dev/fb0") == 0 || strcmp(path, "/dev/fb") == 0) {
        return BFREE_GUEST_OPEN_FB0_FD;
    }
    if (strcmp(path, "/dev/input0") == 0 || strcmp(path, "/dev/input/event0") == 0) {
        return BFREE_GUEST_OPEN_INPUT_FD;
    }
    if (strcmp(path, "/dev/null") == 0) {
        return BFREE_GUEST_OPEN_NULL_FD;
    }
    if (strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/random") == 0) {
        return BFREE_GUEST_OPEN_URANDOM_FD;
    }
    return -1;
}

extern "C" int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int fd;
    va_list ap;

    bfree_guest_trace_evt("open");
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("open");
    /* Qt embeds build-machine paths; fail fast so QLoggingRegistry does not spin. */
    if (path && (strncmp(path, "/root/", 6) == 0 || strstr(path, "qtlogging.ini") != 0)) {
        errno = ENOENT;
        bfree_guest_trace_open_path(path, -1);
        return -1;
    }
    fd = bfree_guest_open_virtual_fd(path);
    if (fd >= 0) {
        bfree_guest_trace_open_path(path, fd);
        return fd;
    }
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
        fd = __real_open(path, flags, mode);
    } else {
        fd = __real_open(path, flags);
    }
    bfree_guest_trace_open_path(path, fd);
    return fd;
}

extern "C" DIR *__real_opendir(const char *name);

extern "C" DIR *__wrap_opendir(const char *name)
{
    bfree_guest_trace_evt("opendir");
    if (name) {
        bfree_guest_trace_open_path(name, -1);
    }
    errno = ENOENT;
    return 0;
}

extern "C" int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return open(path, flags, mode);
}

extern "C" int fstat64(int fd, struct stat *st) { return fstat(fd, st); }
extern "C" int stat64(const char *path, struct stat *st) { return stat(path, st); }

#define BFREE_GUEST_MEMFD_FD 0x3703

extern "C" int memfd_create(const char *name, unsigned int flags)
{
    long r = syscall(319L, (long)(uintptr_t)name, (long)flags);
    if (r < 0)
        return -1;
    return (int)r;
}

extern "C" int ftruncate(int fd, off_t length)
{
    long r = syscall(77L, (long)fd, (long)length);
    return (r < 0) ? -1 : 0;
}

extern "C" int ftruncate64(int fd, off_t length) { return ftruncate(fd, length); }
extern "C" int truncate64(const char *path, off_t length) { return truncate(path, length); }
extern "C" int statfs64(const char *path, struct statfs *buf) { return statfs(path, buf); }
extern "C" int lstat64(const char *path, struct stat *st) { return lstat(path, st); }

extern "C" struct dirent64 *readdir64(DIR *dir)
{
    return (struct dirent64 *)readdir(dir);
}

extern "C" void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, len, prot, flags, fd, offset);
}

/* glibc 2.32+ (Qt/linux-g++); musl has no such symbol — single-threaded guest. */
extern "C" char __libc_single_threaded = 1;

extern "C" int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
                        unsigned int flags)
{
    (void)flags;
    return renameat(olddirfd, oldpath, newdirfd, newpath);
}

extern "C" int close_range(unsigned int first, unsigned int last, unsigned int flags)
{
    unsigned int fd;
    (void)flags;
    for (fd = first; fd <= last; fd++)
        close((int)fd);
    return 0;
}

static const char *bfree_guest_realpath_canon(const char *path);

extern "C" void bfree_guest_serial_step_raw(char step);

extern "C" char *__realpath_chk(const char *path, char *resolved, size_t resolvedlen)
{
    const char *canon;
    size_t n;

    if (!resolved || resolvedlen == 0)
        return 0;
    canon = bfree_guest_realpath_canon(path);
    if (!canon)
        return 0;
    n = strlen(canon);
    if (n + 1u > resolvedlen) {
        errno = ENAMETOOLONG;
        return 0;
    }
    memcpy(resolved, canon, n + 1u);
    bfree_guest_serial_step_raw('W');
    return resolved;
}

extern "C" int __sprintf_chk(char *str, int flag, size_t slen, const char *fmt, ...)
{
    int n;
    va_list ap;
    (void)flag;
    if (!str || slen == 0)
        return 0;
    va_start(ap, fmt);
    n = vsnprintf(str, slen, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" int __fprintf_chk(FILE *stream, int flag, const char *fmt, ...)
{
    int n;
    va_list ap;
    (void)flag;
    va_start(ap, fmt);
    n = __wrap_vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" int __vfprintf_chk(FILE *stream, int flag, const char *fmt, va_list ap)
{
    (void)flag;
    return __wrap_vfprintf(stream, fmt, ap);
}

extern "C" int __snprintf_chk(char *str, int flag, size_t str_len, size_t max_len, const char *fmt, ...)
{
    int n;
    va_list ap;
    (void)flag;
    (void)str_len;
    va_start(ap, fmt);
    n = vsnprintf(str, max_len, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" int __vsnprintf_chk(char *str, int flag, size_t str_len, size_t max_len, const char *fmt,
                              va_list ap)
{
    (void)flag;
    (void)str_len;
    return vsnprintf(str, max_len, fmt, ap);
}

extern "C" char *__strcpy_chk(char *dest, const char *src, size_t destlen)
{
    if (strlen(src) + 1 > destlen)
        abort();
    return strcpy(dest, src);
}

/* glibc LFS64 names; musl on x86_64 uses 64-bit off_t for these APIs. */
extern "C" long long ftello64(FILE *stream) { return (long long)ftello(stream); }

extern "C" int fseeko64(FILE *stream, long long offset, int whence)
{
    return fseeko(stream, (off_t)offset, whence);
}

extern "C" long long lseek64(int fd, long long offset, int whence)
{
    return (long long)lseek(fd, (off_t)offset, whence);
}

/* glibc execinfo; guest has no stack unwinder. */
extern "C" int backtrace(void **buffer, int size)
{
    (void)buffer;
    (void)size;
    return 0;
}

extern "C" int __printf_chk(int flag, const char *fmt, ...)
{
    int n;
    va_list ap;
    (void)flag;
    va_start(ap, fmt);
    n = __wrap_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

extern "C" void __longjmp_chk(jmp_buf env, int val) { longjmp(env, val); }

extern "C" void __cxa_pure_virtual(void)
{
    bfree_guest_serial_lit("[guest] __cxa_pure_virtual\n");
    for (;;)
        __asm__ volatile("pause" ::: "memory");
}

extern "C" size_t __fread_chk(void *ptr, size_t size, size_t nmemb, FILE *stream, size_t destlen)
{
    size_t need = size * nmemb;
    if (need > destlen)
        abort();
    return fread(ptr, size, nmemb, stream);
}

extern "C" int __isoc23_sscanf(const char *str, const char *fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsscanf(str, fmt, ap);
    va_end(ap);
    return n;
}

extern "C" long int __isoc23_strtol(const char *nptr, char **endptr, int base)
{
    return strtol(nptr, endptr, base);
}

extern "C" unsigned long int __isoc23_strtoul(const char *nptr, char **endptr, int base)
{
    return strtoul(nptr, endptr, base);
}

extern "C" int __res_ninit(void *state)
{
    (void)state;
    return -1;
}

extern "C" void __res_nclose(void *state) { (void)state; }

/* Brotli (Qt6Network static); guest ISO has no brotli — stubs return failure / no-op. */
typedef void *BrotliDecoderState;
extern "C" BrotliDecoderState BrotliDecoderCreateInstance(void *(*a)(void *, size_t, size_t),
                                                          void (*f)(void *, void *), void *o)
{
    (void)a;
    (void)f;
    (void)o;
    return (BrotliDecoderState)1;
}
extern "C" void BrotliDecoderDestroyInstance(BrotliDecoderState s) { (void)s; }
extern "C" int BrotliDecoderHasMoreOutput(BrotliDecoderState s)
{
    (void)s;
    return 0;
}
extern "C" const uint8_t *BrotliDecoderTakeOutput(BrotliDecoderState s, size_t *size)
{
    (void)s;
    if (size)
        *size = 0;
    return (const uint8_t *)"";
}
extern "C" int BrotliDecoderDecompressStream(BrotliDecoderState s, size_t *ain, const uint8_t **ainp,
                                             size_t *aout, uint8_t **aoutp, size_t *total_out)
{
    (void)s;
    (void)ain;
    (void)ainp;
    (void)aout;
    (void)aoutp;
    if (total_out)
        *total_out = 0;
    return 0;
}
extern "C" int BrotliDecoderGetErrorCode(BrotliDecoderState s)
{
    (void)s;
    return 0;
}
extern "C" const char *BrotliDecoderErrorString(int code)
{
    (void)code;
    return "brotli-stub";
}

/* libstdc++ futex TU may be omitted from the guest archive; match Qt/libstdc++ ABI.
 * Avoid <chrono> so this TU builds without a full x86_64-elf libstdc++ header tree. */
namespace std {
namespace __atomic_futex_unsigned_base {
struct __futex_sec {
    long __v;
};
struct __futex_nsec {
    long __v;
};

unsigned _M_futex_wait_until(unsigned *__addr, unsigned __val, bool __has_timeout, __futex_sec __s,
                             __futex_nsec __ns)
{
    (void)__val;
    (void)__has_timeout;
    (void)__s;
    (void)__ns;
    if (__addr) {
        *__addr = 0;
    }
    return 1; /* _S_true — woken */
}
} // namespace __atomic_futex_unsigned_base
} // namespace std

/* glibc extension; fall back if musl libc.a lacks it. */
extern "C" int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                             const struct timespec *abstime);

extern "C" int pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                      clockid_t clockid, const struct timespec *abstime)
{
    (void)clockid;
    return __wrap_pthread_cond_timedwait(cond, mutex, abstime);
}

/* pthread_create: prefer kernel CLONE_THREAD (gthr); coop queue is fallback. */
#define BFREE_PTHREAD_SLOTS 8
#define BFREE_PTHREAD_STACK_BYTES (64 * 1024)

struct bfree_pthread_slot {
    pthread_t id;
    void *(*fn)(void *);
    void *arg;
    void *result;
    int joined;
    int coop_alive;
    int use_clone;          /* 1 = real gthr clone path */
    volatile int done;      /* set by child before exit */
    volatile int gate;      /* 0=wait, 1=run fn (handshake so create returns) */
    int gate_efd;
};

static uint8_t g_pthread_stacks[BFREE_PTHREAD_SLOTS][BFREE_PTHREAD_STACK_BYTES]
    __attribute__((aligned(16)));
static volatile struct bfree_pthread_slot *g_pthread_clone_boot;

/* musl treats pthread_t as struct __pthread*; integer ids fault in pthread_getattr_np. */
struct bfree_guest_pthread_obj {
    unsigned char pad[128];
};
static struct bfree_guest_pthread_obj g_pthread_main_obj;
static struct bfree_guest_pthread_obj g_pthread_worker_objs[BFREE_PTHREAD_SLOTS];

static struct bfree_pthread_slot g_pthread_slots[BFREE_PTHREAD_SLOTS];
static volatile int bfree_guest_on_mmap_ctor_stack;
static volatile int bfree_pthread_pump_depth;

static void bfree_pthread_run_pending(void);
static void bfree_pthread_pump_coop_alive(void);
static void bfree_guest_coop_pump_thread_on_exec(void *qthread_ptr);
static void bfree_guest_coop_pump_main_on_exec(void);
static int bfree_guest_exec_rsp_valid(uintptr_t rsp);
static int bfree_guest_qt_on_exec_rsp(uintptr_t exec_rsp);

static void bfree_pthread_pump_coop_alive(void)
{
    for (int i = 0; i < BFREE_PTHREAD_SLOTS; ++i) {
        if (g_pthread_slots[i].coop_alive)
            bfree_guest_coop_pump_thread_on_exec(g_pthread_slots[i].arg);
    }
}

static void bfree_pthread_pump_qv4(void)
{
    /* STAGE 4 deferred ctors clear on_mmap_ctor_stack; qv4_mmap_active covers QQmlEngine. */
    if (bfree_guest_qv4_mmap_active)
        bfree_pthread_run_pending();
}

extern "C" void bfree_guest_serial_hex_u64(uint64_t v);
extern "C" int bfree_guest_qt_coop_pump_thread(void *qthread_ptr);
extern "C" int bfree_guest_qt_coop_pump_main(void);

extern "C" void bfree_guest_qt_coop_schedule(void)
{
    static volatile int depth;
    if (depth)
        return;
    depth = 1;
    bfree_guest_coop_pump_main_on_exec();
    bfree_pthread_run_pending();
    depth = 0;
}

extern "C" int __real_poll(struct pollfd *fds, nfds_t nfds, int timeout);
extern "C" int __real_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

/*
 * Single-threaded cooperative pthread: mutexes are no-ops so a worker started
 * from pthread_create/join can run while the creator still "holds" the lock.
 * Deferred workers also run from poll/sched_yield for Qt background init.
 */
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (bfree_guest_qv4_mmap_active) {
        static unsigned g_qv4_mutex_diag;
        if (g_qv4_mutex_diag < 64u) {
            ++g_qv4_mutex_diag;
            bfree_guest_qv4_trace_tag("mutex_lock");
        }
        if (!bfree_pthread_pump_depth)
            bfree_pthread_pump_qv4();
    } else if (!bfree_guest_on_mmap_ctor_stack) {
        bfree_pthread_run_pending();
    }
    (void)mutex;
    return 0;
}

extern "C" int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (bfree_guest_qv4_mmap_active) {
        static unsigned g_qv4_mutex_unlock_diag;
        if (g_qv4_mutex_unlock_diag < 64u) {
            ++g_qv4_mutex_unlock_diag;
            bfree_guest_qv4_trace_tag("mutex_unlock");
        }
        if (!bfree_pthread_pump_depth)
            bfree_pthread_pump_qv4();
    } else if (!bfree_guest_on_mmap_ctor_stack) {
        bfree_pthread_run_pending();
    }
    (void)mutex;
    return 0;
}

extern "C" int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr;
    if (mutex)
        memset(mutex, 0, sizeof(*mutex));
    return 0;
}

extern "C" int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    if (cond)
        memset(cond, 0, sizeof(*cond));
    return 0;
}

extern "C" int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
    if (bfree_guest_qv4_mmap_active)
        bfree_pthread_pump_qv4();
    else if (!bfree_guest_on_mmap_ctor_stack)
        bfree_pthread_run_pending();
    return 0;
}

extern "C" int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                             const struct timespec *abstime)
{
    (void)abstime;
    return __wrap_pthread_cond_wait(cond, mutex);
}

static unsigned g_wrap_poll_diag;
static unsigned g_wrap_epoll_wait_diag;

static void bfree_guest_run_pending_for_poll(void)
{
    if (bfree_guest_qv4_mmap_active)
        bfree_pthread_pump_qv4();
    else if (!bfree_guest_on_mmap_ctor_stack)
        bfree_pthread_run_pending();
}

extern "C" int __wrap_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    long ret;

    bfree_guest_run_pending_for_poll();
    bfree_guest_trace_evt("epoll_wait");
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("epoll_wait");
    if (g_wrap_epoll_wait_diag < 8u) {
        ++g_wrap_epoll_wait_diag;
        bfree_guest_serial_lit("[wrap] epoll_wait\n");
    }
    if (bfree_guest_qv4_mmap_active)
        timeout = 0;
    ret = syscall(232L, (long)epfd, (long)(uintptr_t)events, (long)maxevents, (long)timeout);
    if (ret > 0)
        return (int)ret;
    if (ret == 0)
        return 0;
    errno = EINTR;
    return -1;
}

extern "C" int __wrap_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    long ret;
    int tries;

    bfree_guest_run_pending_for_poll();
    bfree_guest_trace_evt("poll");
    if (g_wrap_poll_diag < 6u) {
        ++g_wrap_poll_diag;
        bfree_guest_serial_lit("[wrap] poll\n");
    }
    if (!fds || nfds == 0)
        return 0;
    for (tries = 0; tries < 4; ++tries) {
        ret = syscall(7L, (long)(uintptr_t)fds, (long)nfds, (long)timeout);
        if (ret > 0)
            return (int)ret;
        if (ret == 0)
            return 0;
        bfree_guest_run_pending_for_poll();
    }
    errno = EINTR;
    return -1;
}

static unsigned g_wrap_ppoll_diag;

#ifndef POLLIN
#define POLLIN 0x001
#endif

extern "C" int __wrap_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
                            const sigset_t *sigmask)
{
    long ret;
    unsigned diag = g_wrap_ppoll_diag;

    bfree_guest_run_pending_for_poll();
    bfree_guest_trace_evt("ppoll");
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("ppoll");
    if (diag < 12u) {
        ++g_wrap_ppoll_diag;
        bfree_guest_serial_lit("[wrap] ppoll nfds=");
        bfree_guest_serial_hex_u64((uint64_t)nfds);
        if (fds && nfds > 0 && nfds <= 8) {
            for (nfds_t i = 0; i < nfds; ++i) {
                bfree_guest_serial_lit(" fd=");
                bfree_guest_serial_hex_u64((uint64_t)(unsigned)fds[i].fd);
                bfree_guest_serial_lit("/ev=");
                bfree_guest_serial_hex_u64((uint64_t)(unsigned short)fds[i].events);
            }
        }
        bfree_guest_serial_lit("\n");
    }
    if (!fds || nfds == 0) {
        errno = EINTR;
        return -1;
    }
    /*
     * QV4 / QQmlEngine: force timeout=0 so we never hlt-block in the kernel.
     * Pump main ONLY when not already inside bfree_pthread_run_pending — otherwise
     * worker UNIX ppoll → coop_pump_main → sendPostedEvents re-enters and deadlocks
     * (seen: hang right after BFreeInput init during first helper-thread ppoll).
     * Do not fake POLLIN (QThreadPipe wakeUps fail).
     */
    if (bfree_guest_qv4_mmap_active) {
        struct timespec zero = {0, 0};

        if (!bfree_pthread_pump_depth) {
            bfree_pthread_pump_qv4();
            bfree_guest_coop_pump_main_on_exec();
        }
        ret = syscall(271L, (long)(uintptr_t)fds, (long)nfds, (long)(uintptr_t)&zero,
                      (long)(uintptr_t)sigmask);
        if (ret >= 0)
            return (int)ret;
        errno = EINTR;
        return -1;
    }
    ret = syscall(271L, (long)(uintptr_t)fds, (long)nfds, (long)(uintptr_t)timeout,
                  (long)(uintptr_t)sigmask);
    if (diag < 12u) {
        bfree_guest_serial_lit("[wrap] ppoll ret=");
        bfree_guest_serial_hex_u64((uint64_t)(long)ret);
        if (ret > 0 && nfds <= 8) {
            for (nfds_t i = 0; i < nfds; ++i) {
                if (fds[i].revents) {
                    bfree_guest_serial_lit(" rev");
                    bfree_guest_serial_hex_u64((uint64_t)(unsigned short)fds[i].revents);
                }
            }
        }
        bfree_guest_serial_lit("\n");
    }
    if (ret > 0)
        return (int)ret;
    if (ret == 0)
        return 0;
    bfree_guest_run_pending_for_poll();
    ret = syscall(271L, (long)(uintptr_t)fds, (long)nfds, (long)(uintptr_t)timeout,
                  (long)(uintptr_t)sigmask);
    if (ret > 0)
        return (int)ret;
    if (ret == 0)
        return 0;
    errno = EINTR;
    return -1;
}

static unsigned g_wrap_sched_yield_diag;

extern "C" int __wrap_sched_yield(void)
{
    if (bfree_guest_qv4_mmap_active) {
        static unsigned g_qv4_sched_yield_diag;
        if (g_qv4_sched_yield_diag < 48u) {
            ++g_qv4_sched_yield_diag;
            bfree_guest_qv4_trace_tag("sched_yield");
        }
        bfree_pthread_pump_qv4();
    } else if (!bfree_guest_on_mmap_ctor_stack) {
        bfree_pthread_run_pending();
    }
    if (g_wrap_sched_yield_diag < 8u) {
        ++g_wrap_sched_yield_diag;
        bfree_guest_serial_lit("[wrap] sched_yield\n");
    }
    return 0;
}

/* Survive RSP switch: spilled locals would stay on the abandoned exec stack. */
struct bfree_guest_ctor_stack_ctx {
    void (*session_fn)(void);
    uintptr_t saved_rsp;
    void *mmap_stack;
    size_t stack_bytes;
};
static struct bfree_guest_ctor_stack_ctx g_ctor_stack_ctx;

/* desktop.elf: .text @0x2800000 .. .rodata @0x35f2000 (see readelf -S). */
#define BFREE_GUEST_TEXT_LO 0x02800000ULL
#define BFREE_GUEST_TEXT_HI 0x035F2000ULL

static int bfree_guest_ptr_in_text(uintptr_t a)
{
    return a >= BFREE_GUEST_TEXT_LO && a < BFREE_GUEST_TEXT_HI;
}

static int bfree_guest_code_entry_looks_valid(uintptr_t a)
{
    /* VMA in .text is sufficient; byte heuristics rejected valid gcc helpers
     * (e.g. init_static_cond @0x356e900 starts with 48 c7, not push %rbp). */
    return bfree_guest_ptr_in_text(a);
}

/* Redundant guard: exec-stack spill must not clobber session_fn in .bss. */
extern "C" {
uintptr_t bfree_guest_session_fn_guard;
uintptr_t g_mmap_switch_top;
}

extern "C" void bfree_guest_serial_step_raw(char step);
extern "C" __attribute__((noreturn)) void bfree_guest_switch_to_mmap_session_noreturn(void);

static int bfree_guest_fn_ptr_matches_guard(void (*fn)(void))
{
    if (bfree_guest_session_fn_guard == 0)
        return 1;
    return (uintptr_t)(void *)fn == bfree_guest_session_fn_guard;
}

static int bfree_guest_fn_ptr_looks_valid(void (*fn)(void))
{
    if (!bfree_guest_code_entry_looks_valid((uintptr_t)(void *)fn))
        return 0;
    return bfree_guest_fn_ptr_matches_guard(fn);
}

static void bfree_guest_prepare_mmap_stack_top(uintptr_t top)
{
    volatile unsigned char *p;
    size_t i;

    if (top < 4096U)
        return;
    p = (volatile unsigned char *)(top - 4096U);
    for (i = 0; i < 4096U; ++i)
        p[i] = 0;
}

static void bfree_guest_call_saved_session_fn(void)
{
    void (*fn)(void) = g_ctor_stack_ctx.session_fn;

    if (!bfree_guest_fn_ptr_looks_valid(fn)
        && bfree_guest_session_fn_guard != 0
        && bfree_guest_fn_ptr_looks_valid((void (*)(void))bfree_guest_session_fn_guard)) {
        fn = (void (*)(void))bfree_guest_session_fn_guard;
        g_ctor_stack_ctx.session_fn = fn;
    }
    if (!bfree_guest_fn_ptr_looks_valid(fn)) {
        bfree_guest_serial_lit("[desktop_qt] FATAL: bad session_fn=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)fn);
        bfree_guest_serial_lit(" guard=");
        bfree_guest_serial_hex_u64((uint64_t)bfree_guest_session_fn_guard);
        bfree_guest_serial_lit("\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    fn();
}

/* mov rsp + call in one asm blob — fn loaded from .bss guard only (v309). */
static void __attribute__((noreturn, noinline))
bfree_guest_switch_and_invoke_noreturn(uintptr_t top, void (*fn)(void))
{
    uintptr_t guard_fn = bfree_guest_session_fn_guard;

    if (!guard_fn || !bfree_guest_code_entry_looks_valid(guard_fn)) {
        bfree_guest_serial_lit("[desktop_qt] FATAL: invoke bad guard=");
        bfree_guest_serial_hex_u64((uint64_t)guard_fn);
        bfree_guest_serial_lit(" fn=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)fn);
        bfree_guest_serial_lit("\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    if (fn && (uintptr_t)(void *)fn != guard_fn) {
        bfree_guest_serial_lit("[desktop_qt] WARN: invoke fn!=guard, using guard\n");
    }
    g_mmap_switch_top = top;
    g_ctor_stack_ctx.session_fn = (void (*)(void))guard_fn;
    bfree_guest_prepare_mmap_stack_top(top);
    bfree_guest_serial_step_raw('S');
    /* mmap = heap arenas only; keep exec RSP for Qt (movdqa needs 16-byte stack). */
    bfree_guest_call_saved_session_fn();
    bfree_guest_serial_lit("[desktop_qt] FATAL: mmap session returned\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
}

/* Must be inlined — a real call's epilogue (mov rsp,rbp / leave) undoes mov rsp. */
static inline __attribute__((always_inline)) void bfree_guest_switch_rsp(uintptr_t top)
{
    __asm__ volatile("mov %0, %%rsp" : : "r"(top) : "memory");
}

static void bfree_guest_coop_pump_thread_on_exec(void *qthread_ptr)
{
    uintptr_t exec_rsp = g_ctor_stack_ctx.saved_rsp;
    uintptr_t cur_rsp = 0;
    if (bfree_guest_qt_on_exec_rsp(exec_rsp)) {
        __asm__ volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        bfree_guest_switch_rsp(exec_rsp);
        (void)bfree_guest_qt_coop_pump_thread(qthread_ptr);
        bfree_guest_switch_rsp(cur_rsp);
    } else {
        (void)bfree_guest_qt_coop_pump_thread(qthread_ptr);
    }
}

static void bfree_guest_coop_pump_main_on_exec(void)
{
    uintptr_t exec_rsp = g_ctor_stack_ctx.saved_rsp;
    uintptr_t cur_rsp = 0;
    if (bfree_guest_qt_on_exec_rsp(exec_rsp)) {
        __asm__ volatile("mov %%rsp, %0" : "=r"(cur_rsp));
        bfree_guest_switch_rsp(exec_rsp);
        (void)bfree_guest_qt_coop_pump_main();
        bfree_guest_switch_rsp(cur_rsp);
    } else {
        (void)bfree_guest_qt_coop_pump_main();
    }
}

static unsigned g_wrap_pthread_create_diag;

#define BFREE_SYS_clone 56
#define BFREE_SYS_exit 60
#define BFREE_SYS_futex 202
#define BFREE_SYS_ppoll 271
#define BFREE_SYS_eventfd2 290
#define BFREE_CLONE_VM 0x00000100UL
#define BFREE_CLONE_FILES 0x00000400UL
#define BFREE_CLONE_SIGHAND 0x00000800UL
#define BFREE_CLONE_THREAD 0x00010000UL
#define BFREE_FUTEX_WAIT 0
#define BFREE_FUTEX_WAKE 1

static long bfree_pthread_sys6(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}

static void __attribute__((noreturn)) bfree_pthread_clone_child(void)
{
    struct bfree_pthread_slot *slot = (struct bfree_pthread_slot *)g_pthread_clone_boot;
    void *(*fn)(void *);
    void *arg;
    struct pollfd pfd;

    if (!slot) {
        bfree_pthread_sys6(BFREE_SYS_exit, 1, 0, 0, 0, 0, 0);
        for (;;)
            __asm__ volatile("pause");
    }
    /* Park once so parent can return from clone/create, then run start_routine. */
    if (slot->gate_efd >= 0) {
        pfd.fd = slot->gate_efd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)bfree_pthread_sys6(BFREE_SYS_ppoll, (long)&pfd, 1, 0, 0, 0, 0);
    } else {
        while (slot->gate == 0) {
            (void)bfree_pthread_sys6(BFREE_SYS_futex, (long)&slot->gate, BFREE_FUTEX_WAIT, 0, 0, 0, 0);
        }
    }
    fn = slot->fn;
    arg = slot->arg;
    if (fn)
        slot->result = fn(arg);
    slot->fn = 0;
    slot->done = 1;
    (void)bfree_pthread_sys6(BFREE_SYS_futex, (long)&slot->done, BFREE_FUTEX_WAKE, 1, 0, 0, 0);
    bfree_pthread_sys6(BFREE_SYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;)
        __asm__ volatile("pause");
}

static long bfree_pthread_clone_thread(void *child_sp, void *child_fn)
{
    long ret;
    unsigned long flags = BFREE_CLONE_VM | BFREE_CLONE_FILES | BFREE_CLONE_SIGHAND | BFREE_CLONE_THREAD;

    __asm__ volatile(
        "mov %[flags], %%rdi\n\t"
        "mov %[stack], %%rsi\n\t"
        "mov %[fn], %%r9\n\t"
        "mov $56, %%rax\n\t"
        "xor %%rdx, %%rdx\n\t"
        "xor %%r10, %%r10\n\t"
        "xor %%r8, %%r8\n\t"
        "syscall\n\t"
        "test %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        "xor %%rbp, %%rbp\n\t"
        "jmp *%%r9\n\t"
        "1:\n\t"
        : "=a"(ret)
        : [flags] "r"(flags), [stack] "r"(child_sp), [fn] "r"(child_fn)
        : "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory");
    return ret;
}

static int bfree_pthread_create_clone(struct bfree_pthread_slot *slot, int slot_i,
                                      pthread_t *thread, void *(*start_routine)(void *), void *arg)
{
    void *sp;
    long tid;
    uint64_t one = 1;

    slot->id = (pthread_t)(uintptr_t)&g_pthread_worker_objs[slot_i];
    slot->arg = arg;
    slot->result = 0;
    slot->joined = 0;
    slot->coop_alive = 0;
    slot->use_clone = 1;
    slot->done = 0;
    slot->gate = 0;
    slot->fn = start_routine;
    slot->gate_efd = (int)bfree_pthread_sys6(BFREE_SYS_eventfd2, 0, 0, 0, 0, 0, 0);
    g_pthread_clone_boot = slot;
    sp = g_pthread_stacks[slot_i] + BFREE_PTHREAD_STACK_BYTES;
    sp = (void *)(((uintptr_t)sp) & ~(uintptr_t)0xFULL);
    tid = bfree_pthread_clone_thread(sp, (void *)bfree_pthread_clone_child);
    if (tid < 0) {
        slot->fn = 0;
        slot->use_clone = 0;
        slot->gate_efd = -1;
        g_pthread_clone_boot = 0;
        return -1;
    }
    /* Child is parked on gate; release it to run start_routine. */
    if (slot->gate_efd >= 0) {
        (void)bfree_pthread_sys6(1 /* write */, slot->gate_efd, (long)&one, 8, 0, 0, 0);
    } else {
        slot->gate = 1;
        (void)bfree_pthread_sys6(BFREE_SYS_futex, (long)&slot->gate, BFREE_FUTEX_WAKE, 1, 0, 0, 0);
    }
    if (thread)
        *thread = slot->id;
    return 0;
}

extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                              void *(*start_routine)(void *), void *arg)
{
    (void)attr;
    bfree_guest_trace_evt("pthread_create");
    if (g_wrap_pthread_create_diag < 6u) {
        ++g_wrap_pthread_create_diag;
        bfree_guest_serial_lit("[wrap] pthread_create\n");
    }
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("pthread_create");
    for (int i = 0; i < BFREE_PTHREAD_SLOTS; ++i) {
        struct bfree_pthread_slot *slot = &g_pthread_slots[i];
        if (slot->fn == 0 && !slot->use_clone && !slot->coop_alive) {
            if (bfree_pthread_create_clone(slot, i, thread, start_routine, arg) == 0) {
                if (g_wrap_pthread_create_diag <= 6u)
                    bfree_guest_serial_lit("[wrap] pthread_create clone\n");
                return 0;
            }
            /* Fall back to cooperative queue. */
            pthread_t id = (pthread_t)(uintptr_t)&g_pthread_worker_objs[i];
            slot->id = id;
            slot->arg = arg;
            slot->result = 0;
            slot->joined = 0;
            slot->coop_alive = 0;
            slot->use_clone = 0;
            slot->done = 0;
            if (thread)
                *thread = id;
            slot->fn = start_routine;
            if (g_wrap_pthread_create_diag <= 6u)
                bfree_guest_serial_lit("[wrap] pthread_create coop\n");
            return 0;
        }
    }
    return EAGAIN;
}

extern "C" int __wrap_pthread_join(pthread_t thread, void **retval)
{
    for (int i = 0; i < BFREE_PTHREAD_SLOTS; ++i) {
        struct bfree_pthread_slot *slot = &g_pthread_slots[i];
        if (slot->id != thread)
            continue;
        if (slot->use_clone) {
            while (!slot->done) {
                (void)bfree_pthread_sys6(BFREE_SYS_futex, (long)&slot->done, BFREE_FUTEX_WAIT, 0, 0, 0, 0);
            }
            if (retval)
                *retval = slot->result;
            if (slot->gate_efd >= 0) {
                (void)bfree_pthread_sys6(3 /* close */, slot->gate_efd, 0, 0, 0, 0, 0);
                slot->gate_efd = -1;
            }
            slot->id = 0;
            slot->use_clone = 0;
            slot->done = 0;
            slot->result = 0;
            slot->joined = 0;
            return 0;
        }
        if (slot->fn != 0) {
            slot->result = slot->fn(slot->arg);
            slot->fn = 0;
        }
        if (retval)
            *retval = slot->result;
        slot->id = 0;
        slot->joined = 0;
        slot->coop_alive = 0;
        slot->result = 0;
        return 0;
    }
    if (retval)
        *retval = 0;
    return 0;
}

extern "C" int pthread_join(pthread_t thread, void **retval)
{
    return __wrap_pthread_join(thread, retval);
}

/* Static Qt: plugins disabled; satisfy linker if Core still references dl* symbols. */
extern "C" void *dlsym(void *handle, const char *symbol)
{
    (void)handle;
    (void)symbol;
    return 0;
}

extern "C" int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

extern "C" void *__real_memcpy(void *dest, const void *src, size_t n);

static int bfree_guest_ptr_is_poison(const void *p)
{
    uintptr_t u = (uintptr_t)p;

    if (!p)
        return 0;
    if (u == 0x0101010101010101ULL)
        return 1;
    if (u < 0x10000ULL)
        return 1;
    /* Qt tagged null / sentinel band — never dereference in memcpy. */
    if (u >= 0x100000000ULL && u < 0x200000000ULL)
        return 1;
    return 0;
}

extern "C" void *__wrap_memcpy(void *dest, const void *src, size_t n)
{
    if (bfree_guest_qv4_mmap_active && src && bfree_guest_ptr_is_poison(src)) {
        static unsigned g_memcpy_poison_diag;

        if (g_memcpy_poison_diag < 32u) {
            void *ra0 = __builtin_return_address(0);

            ++g_memcpy_poison_diag;
            bfree_guest_serial_lit("[memcpy] poison src=");
            bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)src);
            bfree_guest_serial_lit(" dst=");
            bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)dest);
            bfree_guest_serial_lit(" n=");
            bfree_guest_serial_hex_u64((uint64_t)n);
            bfree_guest_serial_lit(" ra0=");
            bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)ra0);
            bfree_guest_serial_lit("\n");
        }
        if (dest && n)
            memset(dest, 0, n);
        return dest;
    }
    return __real_memcpy(dest, src, n);
}

/* PCRE2 / Qt may be built with _FORTIFY_SOURCE; musl guest has no *_chk helpers. */
extern "C" void *__memcpy_chk(void *dest, const void *src, size_t n, size_t destlen)
{
    if (n > destlen)
        abort();
    return __wrap_memcpy(dest, src, n);
}

extern "C" void *__memmove_chk(void *dest, const void *src, size_t n, size_t destlen)
{
    if (n > destlen)
        abort();
    return memmove(dest, src, n);
}

extern "C" void *__memset_chk(void *dest, int c, size_t n, size_t destlen)
{
    if (n > destlen)
        abort();
    return memset(dest, c, n);
}

/* std::__throw_* / __glibcxx_assert_fail come from hosted libstdc++.a (whole-archive). */

extern "C" {
#include "../userland/desktop_qt/guest_serial.h"
#include <elf.h>
#include <string.h>
#include <stdlib.h>
void __init_tls(size_t *);
void __init_ssp(void *);
}

/* musl malloc-ng context (malloc.o .bss, 928 bytes). Zero before init_array; stock __malloc_alloc_meta inits. */
#define BFREE_MUSL_MALLOC_CONTEXT_BYTES 928U
extern "C" char __malloc_context[];

/* Qt static ctors exhaust the exec stack; dedicated buffer + per-ctor RSP reset.
 * Keep .bss modest so ELF PT_LOAD maps reliably. */
#define BFREE_GUEST_CTOR_STACK_BYTES (8U * 1024U * 1024U)
/* mmap ctor stack: fixed VA above desktop.elf PT_LOAD (~0x07F67048), not g_guest_heap_next
 * (0x03C00000+). Low-heap mmap stacks grow down into ELF .bss and corrupt QList/realloc. */
#define BFREE_GUEST_CTOR_MMAP_VA          0x08000000u
/* 256 MiB ctor stack (full 0x08000000-0x18000000 window). Bump mmap only for QML hybrid. */
#define BFREE_GUEST_CTOR_MMAP_STACK_BYTES (256U * 1024U * 1024U)
#define BFREE_GUEST_CTOR_MMAP_STACK_BYTES_HYBRID (224U * 1024U * 1024U)
#define BFREE_GUEST_CTOR_BUMP_MMAP_VA     (BFREE_GUEST_CTOR_MMAP_VA + BFREE_GUEST_CTOR_MMAP_STACK_BYTES_HYBRID)
#define BFREE_GUEST_CTOR_BUMP_MMAP_BYTES  (32U * 1024U * 1024U)
/* musl/Qt anon mmap above this hits kernel identity PTEs (ERR=5) when heap tail is full. */
#define BFREE_GUEST_BRK_HEAP_CEIL         0x07000000u
/* QV4 JS/GC stacks + MemoryManager chunks (PageAllocation). */
#define BFREE_GUEST_QV4_MMAP_VA           0x18000000u
#define BFREE_GUEST_QV4_MMAP_BYTES        (16U * 1024U * 1024U)
#define BFREE_GUEST_MEMFD_FD              0x3703
#define BFREE_GUEST_EXEC_STACK_FLOOR      0x00200000ULL
/* AMD64 SysV: RSP%16==0 immediately before CALL (after call, entry RSP%16==8). */
#define BFREE_GUEST_CTOR_STACK_CALL_BIAS    0U
/* Fixed overflow heap above QV4 arena (16MB @ 0x18000000 ends 0x19000000). */
#define BFREE_GUEST_FALLBACK_HEAP_VA      0x19000000u
#define BFREE_GUEST_FALLBACK_HEAP_BYTES   (128U * 1024U * 1024U)
/* Single QV4 MemoryManager slab above fallback (0x19000000+128MiB = 0x21000000). */
#define BFREE_GUEST_QV4_LARGE_VA          0x21000000u
#define BFREE_GUEST_QV4_LARGE_BYTES       (128U * 1024U * 1024U)
#define BFREE_GUEST_QV4_LARGE_THRESHOLD   (16U * 1024U * 1024U)
/* Match kernel/sysmain/syscall.c — do not rely on hosted sys/mman.h at link time. */
#define BFREE_MAP_PRIVATE                 0x02L
#define BFREE_MAP_ANONYMOUS               0x20L
#define BFREE_MAP_FIXED                   0x10L

static unsigned char bfree_guest_ctor_stack_buf[BFREE_GUEST_CTOR_STACK_BYTES]
    __attribute__((aligned(4096)));

static void *bfree_guest_alloc_ctor_stack(uintptr_t *top_out)
{
    uintptr_t top = (uintptr_t)bfree_guest_ctor_stack_buf + BFREE_GUEST_CTOR_STACK_BYTES - 256U;
    if (top_out)
        *top_out = top;
    return bfree_guest_ctor_stack_buf;
}

extern "C" long syscall(long number, ...);
extern "C" void bfree_guest_serial_hex_u64(uint64_t v);

static long bfree_guest_mmap_fixed_try(uintptr_t va, size_t bytes);

/* libc mmap() ignores MAP_FIXED; use syscall(9/26) at a fixed VA above desktop.elf. */
static int bfree_guest_mmap_ctor_stack_reset;

static void *bfree_guest_mmap_ctor_stack_at(size_t stack_bytes, uintptr_t *top_out)
{
    uintptr_t va = (uintptr_t)BFREE_GUEST_CTOR_MMAP_VA;
    static uintptr_t g_mmap_va;
    static size_t g_mmap_bytes;
    static size_t g_mmap_actual;
    static const size_t k_try[] = {
        256U * 1024U * 1024U,
        64U * 1024U * 1024U,
        32U * 1024U * 1024U,
    };
    long ret;
    unsigned i;
    size_t n;

    if (!top_out || stack_bytes == 0)
        return 0;
    if (bfree_guest_mmap_ctor_stack_reset) {
        g_mmap_va = 0;
        g_mmap_bytes = 0;
        g_mmap_actual = 0;
        bfree_guest_mmap_ctor_stack_reset = 0;
    }
    if (g_mmap_va && g_mmap_bytes == stack_bytes && g_mmap_actual) {
        *top_out = g_mmap_va + g_mmap_actual - 256U - BFREE_GUEST_CTOR_STACK_CALL_BIAS;
        return (void *)g_mmap_va;
    }
    g_mmap_va = 0;
    g_mmap_bytes = 0;
    g_mmap_actual = 0;
    /* APP: Linux mmap is 9. Native mmap is 26. 256MiB may ENOMEM; hello only needs 32MiB. */
    for (i = 0; i < (unsigned)(sizeof(k_try) / sizeof(k_try[0])); i++) {
        n = k_try[i];
        if (n > stack_bytes)
            continue;
        ret = bfree_guest_mmap_fixed_try(va, n);
        if (ret >= 0 && (uintptr_t)ret == va) {
            g_mmap_va = va;
            g_mmap_bytes = stack_bytes;
            g_mmap_actual = n;
            *top_out = va + n - 256U - BFREE_GUEST_CTOR_STACK_CALL_BIAS;
            bfree_guest_serial_lit("[desktop_qt] ctor mmap ok n=");
            bfree_guest_serial_hex_u64((uint64_t)n);
            bfree_guest_serial_lit("\n");
            return (void *)va;
        }
    }
    bfree_guest_serial_lit("[desktop_qt] ctor mmap fail bytes=");
    bfree_guest_serial_hex_u64((uint64_t)stack_bytes);
    bfree_guest_serial_lit("\n");
    return 0;
}

static void bfree_pthread_run_pending(void)
{
    if (bfree_pthread_pump_depth)
        return;
    bfree_pthread_pump_depth = 1;
    for (int i = 0; i < BFREE_PTHREAD_SLOTS; ++i) {
        struct bfree_pthread_slot *slot = &g_pthread_slots[i];
        void *(*fn)(void *);
        void *arg;
        uintptr_t exec_rsp;
        uintptr_t cur_rsp;

        if (slot->use_clone)
            continue; /* kernel gthr owns this slot */
        if (slot->coop_alive) {
            bfree_guest_coop_pump_thread_on_exec(slot->arg);
            continue;
        }
        if (slot->fn == 0)
            continue;
        fn = slot->fn;
        arg = slot->arg;
        slot->fn = 0;
        slot->coop_alive = 1;
        slot->joined = 0;
        exec_rsp = g_ctor_stack_ctx.saved_rsp;
        if (exec_rsp != 0 && bfree_guest_exec_rsp_valid(exec_rsp)
            && (bfree_guest_on_mmap_ctor_stack || bfree_guest_qv4_mmap_active)) {
            if (bfree_guest_qv4_mmap_active) {
                bfree_guest_serial_lit("[qv4] pthread_start exec_rsp=");
                bfree_guest_serial_hex_u64((uint64_t)exec_rsp);
                bfree_guest_serial_lit("\n");
            }
            __asm__ volatile("mov %%rsp, %0" : "=r"(cur_rsp));
            bfree_guest_switch_rsp(exec_rsp);
            slot->result = fn(arg);
            bfree_guest_switch_rsp(cur_rsp);
        } else {
            slot->result = fn(arg);
        }
    }
    bfree_pthread_pump_depth = 0;
}

extern "C" int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr)
{
    void *stack_base;
    size_t stack_sz;

    (void)thread;
    if (!attr) {
        errno = EINVAL;
        return EINVAL;
    }
    /* Do not call pthread_attr_init — it takes __acquire_ptc rwlock (futex) on B-Free. */
    memset(attr, 0, sizeof(*attr));
    if (g_ctor_stack_ctx.mmap_stack && g_ctor_stack_ctx.stack_bytes) {
        stack_base = g_ctor_stack_ctx.mmap_stack;
        stack_sz = g_ctor_stack_ctx.stack_bytes;
    } else {
        stack_base = (void *)(uintptr_t)0x00100000UL;
        stack_sz = (size_t)(8U << 20);
    }
    return pthread_attr_setstack(attr, stack_base, stack_sz);
}

extern "C" int pthread_rwlock_rdlock(pthread_rwlock_t *rw)
{
    (void)rw;
    return 0;
}

extern "C" int pthread_rwlock_wrlock(pthread_rwlock_t *rw)
{
    (void)rw;
    return 0;
}

extern "C" int pthread_rwlock_unlock(pthread_rwlock_t *rw)
{
    (void)rw;
    return 0;
}

/*
 * .init_array may contain EH .cold paths or qCleanup/initializerD1Ev tails — never run those.
 * Large _GLOBAL__sub_I_qrc_* (qInit + __cxa_atexit) defer to guest_main on mmap stack.
 * Address-based ranges were too broad (skipped GuestBFreeShellProcess meta ctors).
 */
static uintptr_t bfree_guest_rel32_target(uintptr_t from, unsigned off)
{
    const unsigned char *fn = (const unsigned char *)from;
    int32_t rel;

    memcpy(&rel, fn + off + 1, 4);
    return from + off + 5U + (uintptr_t)(int64_t)rel;
}

static int bfree_guest_skip_init_array_ctor(uintptr_t addr)
{
    const unsigned char *fn = (const unsigned char *)addr;
    uintptr_t target;
    int i;

    if (fn[0] == 0xe8) {
        target = bfree_guest_rel32_target(addr, 0);
        if (target >= 0x36700000ULL && target < 0x36800000ULL)
            return 1;
    }
    if (fn[0] == 0xe9) {
        target = bfree_guest_rel32_target(addr, 0);
        if (target >= 0x32f20000ULL && target < 0x32f30000ULL)
            return 1;
    }
    if (fn[0] == 0xf3 && fn[1] == 0x0f && fn[2] == 0x1e && fn[3] == 0xfa) {
        for (i = 4; i < 48; ++i) {
            if (fn[i] == 0xe9) {
                target = bfree_guest_rel32_target(addr, (unsigned)i);
                if (target >= 0x32f20000ULL && target < 0x32f30000ULL)
                    return 1;
            }
        }
    }
    return 0;
}

static int bfree_guest_addr_in_desktop_image(uintptr_t a)
{
    return a >= 0x02800000ULL && a < 0x08000000ULL;
}

static int bfree_guest_ctor_ptr_looks_valid(void (*fn)(void))
{
    return bfree_guest_fn_ptr_looks_valid(fn);
}

static int bfree_guest_defer_qrc_ctor(uintptr_t addr)
{
    const unsigned char *fn = (const unsigned char *)addr;

    if (!bfree_guest_addr_in_desktop_image(addr))
        return 0;
    /*
     * Defer all qrc to guest_ctor_qml_phase on one hybrid session (v79).
     * init_array pre-registration leaves QList on a dead bump session.
     */
    if (fn[0] == 0x55 && fn[1] == 0x48 && fn[2] == 0x89 && fn[3] == 0xe5 && fn[4] == 0xe8)
        return 1;
    if (fn[0] == 0xf3 && fn[1] == 0x0f && fn[2] == 0x1e && fn[3] == 0xfa && fn[4] == 0x48 && fn[5] == 0x83 && fn[6] == 0xec)
        return 1;
    return 0;
}

static int bfree_guest_is_qrc_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;

    if (!bfree_guest_ctor_ptr_looks_valid(fn))
        return 0;
    if (b[0] == 0x55 && b[1] == 0x48 && b[2] == 0x89 && b[3] == 0xe5 && b[4] == 0xe8)
        return 1;
    if (b[0] == 0xf3 && b[1] == 0x0f && b[2] == 0x1e && b[3] == 0xfa && b[4] == 0x48 && b[5] == 0x83 && b[6] == 0xec)
        return 1;
    return 0;
}

#ifndef BFREE_GUEST_INIT_ARRAY_DEFER_FROM
#define BFREE_GUEST_INIT_ARRAY_DEFER_FROM 8u
#endif

#define BFREE_GUEST_DEFERRED_CTORS_MAX 48u
static void (*g_deferred_ctors[BFREE_GUEST_DEFERRED_CTORS_MAX])(void);
static unsigned g_deferred_ctor_idx[BFREE_GUEST_DEFERRED_CTORS_MAX];
static unsigned g_deferred_ctor_count;

static int bfree_guest_defer_late_ctor(uintptr_t addr, unsigned array_index)
{
    const unsigned char *fn = (const unsigned char *)addr;

    if (!bfree_guest_addr_in_desktop_image(addr))
        return 0;
    if (array_index >= (unsigned)BFREE_GUEST_INIT_ARRAY_DEFER_FROM)
        return 1;
    /* QWindowSystemInterface static init (ctor[8] @ ~0x28c9e20): NULL deref in init_array. */
    if (fn[0] == 0x49 && fn[1] == 0x8b && fn[2] == 0x7e && fn[3] == 0x08 && fn[4] == 0x48 && fn[5] == 0x8d
        && fn[6] == 0x50)
        return 1;
    return 0;
}

static void bfree_guest_record_deferred_ctor(void (*fn)(void), unsigned array_index)
{
    if (!fn || g_deferred_ctor_count >= BFREE_GUEST_DEFERRED_CTORS_MAX)
        return;
    g_deferred_ctors[g_deferred_ctor_count] = fn;
    g_deferred_ctor_idx[g_deferred_ctor_count] = array_index;
    ++g_deferred_ctor_count;
}

/* Bump heap only until TLS+auxv are ready; ctor-stack plugin init uses mmap bump arena. */
static unsigned char bfree_guest_bump_heap[4 * 1024 * 1024] __attribute__((aligned(16)));
static size_t bfree_guest_bump_off;
static size_t bfree_guest_ctor_bump_off;
static volatile int bfree_guest_musl_malloc_ready;
static volatile int bfree_guest_ctor_bump_mode;
static volatile int bfree_guest_ctor_bump_hybrid;
static volatile int bfree_guest_qrc_alloc_scope;
static unsigned char *bfree_guest_ctor_bump_base;
static size_t bfree_guest_ctor_bump_cap;
static unsigned char *bfree_guest_fallback_heap_base;
static size_t bfree_guest_fallback_heap_cap;
static size_t bfree_guest_fallback_heap_off;
static uintptr_t bfree_guest_qv4_mmap_next;
static uintptr_t bfree_guest_qv4_mmap_end;
static volatile int bfree_guest_qv4_large_used;

static int bfree_guest_mmap_fixed_anon(uintptr_t va, size_t bytes);
static void *bfree_guest_mmap_qv4_arena(size_t length);
static void *bfree_guest_qv4_large_mmap(size_t length);
extern "C" int bfree_guest_ensure_fallback_heap(void);

static int bfree_guest_qv4_ensure_arena(void)
{
    if (bfree_guest_qv4_mmap_next == 0)
        bfree_guest_qv4_mmap_next = (uintptr_t)BFREE_GUEST_QV4_MMAP_VA;
    if (bfree_guest_qv4_mmap_end == 0)
        bfree_guest_qv4_mmap_end = bfree_guest_qv4_mmap_next + (uintptr_t)BFREE_GUEST_QV4_MMAP_BYTES;
    return 0;
}

extern "C" void bfree_guest_qv4_heartbeat(const char *tag)
{
    if (!bfree_guest_qv4_mmap_active || !tag)
        return;
    bfree_guest_serial_lit("[qv4] hb ");
    bfree_guest_serial_lit(tag);
    bfree_guest_serial_lit("\n");
    /* Do not pump pthread/Qt here — re-entering the event loop from QV4 ctor hangs. */
}

static void *bfree_guest_qv4_active_engine_ptr;

extern "C" void bfree_guest_qv4_set_active_engine(void *engine)
{
    bfree_guest_qv4_active_engine_ptr = engine;
}

extern "C" void *bfree_guest_qv4_active_engine(void)
{
    return bfree_guest_qv4_active_engine_ptr;
}

extern "C" void bfree_guest_qv4_preflight_arena(void)
{
    if (bfree_guest_qv4_ensure_arena() != 0) {
        bfree_guest_serial_lit("[desktop_qt] qv4 arena preflight fail\n");
    } else {
        bfree_guest_serial_lit("[desktop_qt] qv4 arena preflight va=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)BFREE_GUEST_QV4_MMAP_VA);
        bfree_guest_serial_lit("\n");
    }
}

extern "C" void bfree_guest_qv4_mmap_scope_begin(void)
{
    g_qv4_trace_tag_n = 0;
    bfree_guest_qv4_mmap_active = 1;
    if (bfree_guest_qv4_ensure_arena() != 0) {
        bfree_guest_serial_lit("[desktop_qt] qv4 arena mmap fail\n");
    } else {
        bfree_guest_serial_lit("[desktop_qt] qv4 arena ok va=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)BFREE_GUEST_QV4_MMAP_VA);
        bfree_guest_serial_lit("\n");
    }
    bfree_guest_qv4_trace_tag("scope_begin");
}

extern "C" void bfree_guest_qv4_mmap_scope_end(void)
{
    bfree_guest_qv4_trace_tag("scope_end");
    bfree_guest_trace_enable(0);
    bfree_guest_qv4_mmap_active = 0;
}

/* Replaces guest_mmap.o: block anon mmap during ctor (musl lands on identity PTEs). */
extern "C" void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    long ret;
    long lflags = (long)flags;
    long anon = (fd < 0) || (lflags & BFREE_MAP_ANONYMOUS);

    if (length == 0 || length > (size_t)0x7ffffffeUL)
        return (void *)-1L;
    if ((lflags & BFREE_MAP_FIXED) && addr == 0)
        lflags &= ~BFREE_MAP_FIXED;
    if (bfree_guest_ctor_bump_mode && bfree_guest_ctor_bump_hybrid && fd == (int)BFREE_GUEST_MEMFD_FD
        && (bfree_guest_qv4_mmap_active || bfree_guest_on_mmap_ctor_stack)) {
        void *qv4 = bfree_guest_mmap_qv4_arena(length);
        if (qv4 != (void *)-1L) {
            bfree_guest_trace_mmap((unsigned long)length, (long)(uintptr_t)qv4);
            return qv4;
        }
    }
    if (bfree_guest_ctor_bump_mode && anon && !(lflags & BFREE_MAP_FIXED)) {
        /* QV4 OSAllocator: prefer pre-mapped arena over exhausted kernel sliding heap. */
        if (bfree_guest_ctor_bump_hybrid
            && (bfree_guest_qv4_mmap_active || bfree_guest_on_mmap_ctor_stack)) {
            void *qv4 = bfree_guest_mmap_qv4_arena(length);
            if (qv4 != (void *)-1L) {
                bfree_guest_trace_mmap((unsigned long)length, (long)(uintptr_t)qv4);
                return qv4;
            }
        }
        ret = syscall(9L, (long)(uintptr_t)addr, (long)length, (long)prot, lflags, (long)fd, (long)offset);
        if (ret < 0)
            ret = syscall(26L, (long)(uintptr_t)addr, (long)length, (long)prot, lflags, (long)fd, (long)offset);
        if (ret < 0 && bfree_guest_ctor_bump_hybrid
            && (bfree_guest_qv4_mmap_active || bfree_guest_on_mmap_ctor_stack)) {
            void *qv4 = bfree_guest_mmap_qv4_arena(length);
            if (qv4 != (void *)-1L) {
                bfree_guest_trace_mmap((unsigned long)length, (long)(uintptr_t)qv4);
                return qv4;
            }
        }
        bfree_guest_trace_mmap((unsigned long)length, ret);
        if (ret >= 0)
            return (void *)(uintptr_t)ret;
        return (void *)-1L;
    }
    ret = syscall(26L, (long)(uintptr_t)addr, (long)length, (long)prot, lflags, (long)fd, (long)offset);
    if (ret < 0)
        ret = syscall(9L, (long)(uintptr_t)addr, (long)length, (long)prot, lflags, (long)fd, (long)offset);
    bfree_guest_trace_mmap((unsigned long)length, ret);
    if (ret < 0)
        return (void *)-1L;
    if (anon && !(lflags & BFREE_MAP_FIXED)) {
        uint64_t r = (uint64_t)(uintptr_t)ret;
        /* Reject identity-PTE band only; kernel heap at 0x19xxxxxx is valid. */
        if (r >= (uint64_t)BFREE_GUEST_BRK_HEAP_CEIL && r < (uint64_t)BFREE_GUEST_CTOR_MMAP_VA)
            return (void *)-1L;
    }
    return (void *)(uintptr_t)ret;
}

extern "C" void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return __mmap(addr, length, prot, flags, fd, offset);
}

/* APP role uses Linux numbers: 9=mmap, 26=msync. INIT/native still has 26=mmap.
 * p8test is APP. Do not treat msync's 0 as MAP_FIXED success (that printed
 * ctor mmap fail ret=0). */
static long bfree_guest_mmap_fixed_try(uintptr_t va, size_t bytes)
{
    long flags = BFREE_MAP_PRIVATE | BFREE_MAP_ANONYMOUS | BFREE_MAP_FIXED;
    long ret9;
    long ret26;

    ret9 = syscall(9L, (long)va, (long)bytes, 3L, flags, -1L, 0L);
    if (ret9 >= 0 && (uintptr_t)ret9 == va)
        return ret9;
    ret26 = syscall(26L, (long)va, (long)bytes, 3L, flags, -1L, 0L);
    if (ret26 >= 0 && (uintptr_t)ret26 == va)
        return ret26;
    bfree_guest_serial_lit("[desktop_qt] mmap9=");
    bfree_guest_serial_hex_u64((uint64_t)(unsigned long)ret9);
    bfree_guest_serial_lit(" mmap26=");
    bfree_guest_serial_hex_u64((uint64_t)(unsigned long)ret26);
    bfree_guest_serial_lit(" want=");
    bfree_guest_serial_hex_u64((uint64_t)va);
    bfree_guest_serial_lit(" n=");
    bfree_guest_serial_hex_u64((uint64_t)bytes);
    bfree_guest_serial_lit("\n");
    return (ret9 < 0) ? ret9 : -1L;
}

static int bfree_guest_mmap_fixed_anon(uintptr_t va, size_t bytes)
{
    long ret;

    if (bytes == 0)
        return -1;
    ret = bfree_guest_mmap_fixed_try(va, bytes);
    if (ret < 0 || (uintptr_t)ret != va)
        return -1;
    return 0;
}

static void *bfree_guest_mmap_qv4_arena(size_t length)
{
    const size_t page = 4096U;
    size_t need;
    uintptr_t va;
    long flags;
    long ret;

    if (length == 0)
        return (void *)-1L;
    need = (length + page - 1U) & ~(page - 1U);
    if (bfree_guest_qv4_ensure_arena() != 0)
        return (void *)-1L;
    va = bfree_guest_qv4_mmap_next;
    if (va + need > bfree_guest_qv4_mmap_end)
        return (void *)-1L;
    flags = BFREE_MAP_PRIVATE | BFREE_MAP_ANONYMOUS | BFREE_MAP_FIXED;
    ret = syscall(9L, (long)va, (long)need, 3L, flags, -1L, 0L);
    if (ret < 0)
        ret = syscall(26L, (long)va, (long)need, 3L, flags, -1L, 0L);
    if (ret < 0 || (uintptr_t)ret != va)
        return (void *)-1L;
    bfree_guest_qv4_mmap_next = va + need;
    return (void *)va;
}

static void *bfree_guest_qv4_large_mmap(size_t length)
{
    const size_t page = 4096U;
    size_t need;
    uintptr_t va;
    long flags;
    long ret;

    if (length < BFREE_GUEST_QV4_LARGE_THRESHOLD || bfree_guest_qv4_large_used)
        return 0;
    need = (length + page - 1U) & ~(page - 1U);
    if (need > BFREE_GUEST_QV4_LARGE_BYTES)
        return 0;
    va = (uintptr_t)BFREE_GUEST_QV4_LARGE_VA;
    flags = BFREE_MAP_PRIVATE | BFREE_MAP_ANONYMOUS | BFREE_MAP_FIXED;
    ret = syscall(9L, (long)va, (long)need, 3L, flags, -1L, 0L);
    if (ret < 0)
        ret = syscall(26L, (long)va, (long)need, 3L, flags, -1L, 0L);
    if (ret < 0 || (uintptr_t)ret != va) {
        bfree_guest_serial_lit("[desktop_qt] qv4 large mmap fail ret=");
        bfree_guest_serial_hex_u64((uint64_t)(long)ret);
        bfree_guest_serial_lit("\n");
        return 0;
    }
    bfree_guest_qv4_large_used = 1;
    bfree_guest_serial_lit("[desktop_qt] qv4 large slab n=");
    bfree_guest_serial_hex_u64((uint64_t)length);
    bfree_guest_serial_lit(" va=");
    bfree_guest_serial_hex_u64((uint64_t)va);
    bfree_guest_serial_lit("\n");
    return (void *)va;
}

static int bfree_guest_mmap_ctor_bump_arena(void)
{
    uintptr_t va = (uintptr_t)BFREE_GUEST_CTOR_BUMP_MMAP_VA;
    size_t bytes = BFREE_GUEST_CTOR_BUMP_MMAP_BYTES;

    if (bfree_guest_ctor_bump_base && bfree_guest_ctor_bump_cap >= bytes)
        return 0;
    if (bfree_guest_mmap_fixed_anon(va, bytes) != 0)
        return -1;
    bfree_guest_ctor_bump_base = (unsigned char *)va;
    bfree_guest_ctor_bump_cap = bytes;
    return 0;
}

extern "C" void *__real_malloc(size_t n);
extern "C" void *__real_calloc(size_t n, size_t sz);
extern "C" void *__real_realloc(void *p, size_t n);
extern "C" void __real_free(void *p);

static void *bfree_guest_bump_alloc_raw(unsigned char *base, size_t cap, size_t *off, size_t n)
{
    size_t need = (n + sizeof(size_t) + 15U) & ~15U;
    unsigned char *blk;

    if (need == 0)
        need = 16;
    if (!base || !off || *off + need > cap)
        return 0;
    blk = base + *off;
    *(size_t *)blk = n;
    *off += need;
    return blk + sizeof(size_t);
}

static int bfree_guest_ptr_in_bump_heap(void *p)
{
    uintptr_t u = (uintptr_t)p;

    if (!p)
        return 0;
    if (bfree_guest_ctor_bump_base
        && u >= (uintptr_t)bfree_guest_ctor_bump_base + sizeof(size_t)
        && u < (uintptr_t)bfree_guest_ctor_bump_base + bfree_guest_ctor_bump_cap)
        return 1;
    if (u >= (uintptr_t)bfree_guest_bump_heap + sizeof(size_t)
        && u < (uintptr_t)bfree_guest_bump_heap + sizeof(bfree_guest_bump_heap))
        return 1;
    return 0;
}

static size_t bfree_guest_bump_user_size(void *p)
{
    unsigned char *u = (unsigned char *)p;

    if (!bfree_guest_ptr_in_bump_heap(p))
        return 0;
    return *(size_t *)(u - sizeof(size_t));
}

static void *bfree_guest_bump_alloc(size_t n)
{
    void *p;

    if (bfree_guest_ctor_bump_mode && bfree_guest_ctor_bump_base)
        p = bfree_guest_bump_alloc_raw(bfree_guest_ctor_bump_base, bfree_guest_ctor_bump_cap,
                                         &bfree_guest_ctor_bump_off, n);
    else
        p = bfree_guest_bump_alloc_raw(bfree_guest_bump_heap, sizeof(bfree_guest_bump_heap),
                                       &bfree_guest_bump_off, n);
    if (!p && bfree_guest_ctor_bump_mode) {
        bfree_guest_serial_lit("[desktop_qt] bump alloc fail\n");
        return 0;
    }
    if (p && bfree_guest_ctor_bump_mode && n && !bfree_guest_on_mmap_ctor_stack)
        memset(p, 0, n);
    return p;
}

static void *bfree_guest_bump_realloc(void *old, size_t n)
{
    size_t old_sz;
    void *p;

    if (!old)
        return bfree_guest_bump_alloc(n);
    old_sz = bfree_guest_bump_user_size(old);
    p = bfree_guest_bump_alloc(n);
    if (p && old_sz)
        memcpy(p, old, old_sz < n ? old_sz : n);
    return p;
}

static void *bfree_guest_mmap_fallback_alloc(size_t n);
static int bfree_guest_ptr_is_mmap_fallback(void *p);
static int bfree_guest_fallback_resolve_block(void *p, void **hdr_out, size_t *sz_out);
static size_t bfree_guest_fallback_user_size(void *p);
static void *bfree_guest_fallback_realloc(void *old, size_t n);
static void bfree_guest_fill_auxv_tables(void);

extern "C" size_t malloc_usable_size(void *p);
extern "C" size_t __real_malloc_usable_size(void *p) __attribute__((weak));

static size_t bfree_guest_malloc_usable_size(void *p)
{
    size_t sz;

    if (!p)
        return 0;
    if (bfree_guest_musl_malloc_ready && !bfree_guest_ctor_bump_mode && __real_malloc_usable_size) {
        sz = __real_malloc_usable_size(p);
        if (sz)
            return sz;
    }
    sz = bfree_guest_fallback_user_size(p);
    if (sz)
        return sz;
    if (bfree_guest_ctor_bump_mode || bfree_guest_ctor_bump_base) {
        sz = bfree_guest_bump_user_size(p);
        if (sz)
            return sz;
    }
    if (__real_malloc_usable_size)
        return __real_malloc_usable_size(p);
    return 0;
}

extern "C" size_t __wrap_malloc_usable_size(void *p)
{
    return bfree_guest_malloc_usable_size(p);
}

static void *bfree_guest_shallow_musl_realloc(void *old, size_t n)
{
    size_t old_sz = 0;
    void *p;

    if (!old)
        return __real_malloc(n);
    if (n == 0) {
        __real_free(old);
        return 0;
    }
    old_sz = bfree_guest_malloc_usable_size(old);
    p = __real_malloc(n);
    if (!p)
        return 0;
    if (old_sz)
        memcpy(p, old, old_sz < n ? old_sz : n);
    __real_free(old);
    return p;
}

/* hybrid: malloc may be musl while old realloc used bump — route musl ptrs through shallow realloc. */
static void *bfree_guest_hybrid_realloc(void *old, size_t n)
{
    size_t old_sz;
    void *p;

    if (!old) {
        if (bfree_guest_musl_malloc_ready) {
            bfree_guest_fill_auxv_tables();
            p = __real_malloc(n);
            if (!p)
                p = bfree_guest_mmap_fallback_alloc(n);
            if (p)
                return p;
        }
        return bfree_guest_bump_alloc(n);
    }
    if (n == 0) {
        if (!bfree_guest_ptr_in_bump_heap(old) && !bfree_guest_ptr_is_mmap_fallback(old))
            __real_free(old);
        return 0;
    }
    {
        void *copy_from = old;

        if (bfree_guest_fallback_resolve_block(old, &copy_from, &old_sz)) {
            p = bfree_guest_mmap_fallback_alloc(n);
            if (!p && bfree_guest_musl_malloc_ready) {
                bfree_guest_fill_auxv_tables();
                p = __real_malloc(n);
            }
            if (!p)
                return 0;
            if (old_sz)
                memcpy(p, copy_from, old_sz < n ? old_sz : n);
            return p;
        }
    }
    if (bfree_guest_musl_malloc_ready) {
        if (bfree_guest_ptr_in_bump_heap(old) || bfree_guest_ptr_is_mmap_fallback(old)) {
            void *copy_from = old;

            if (bfree_guest_ptr_is_mmap_fallback(old))
                old_sz = bfree_guest_fallback_user_size(old);
            else
                old_sz = bfree_guest_bump_user_size(old);
            bfree_guest_fill_auxv_tables();
            p = bfree_guest_mmap_fallback_alloc(n);
            if (!p)
                p = __real_malloc(n);
            if (!p)
                return bfree_guest_bump_realloc(old, n);
            if (old_sz)
                memcpy(p, copy_from, old_sz < n ? old_sz : n);
            return p;
        }
        bfree_guest_fill_auxv_tables();
        return bfree_guest_shallow_musl_realloc(old, n);
    }
    return bfree_guest_bump_realloc(old, n);
}

extern "C" void bfree_guest_enable_musl_malloc(void)
{
    bfree_guest_musl_malloc_ready = 1;
}

extern "C" int bfree_guest_ensure_fallback_heap(void);

extern "C" void bfree_guest_qrc_alloc_scope_enter(void)
{
    bfree_guest_qrc_alloc_scope = 1;
    if (bfree_guest_ensure_fallback_heap() != 0)
        return;
    bfree_guest_serial_lit("[desktop_qt] alloc: qrc arena scope enter (fallback only)\n");
}

extern "C" void bfree_guest_qrc_alloc_scope_leave(void)
{
    bfree_guest_qrc_alloc_scope = 0;
    bfree_guest_serial_lit("[desktop_qt] alloc: qrc arena scope leave\n");
}

extern "C" void bfree_guest_set_force_fallback_alloc(int on)
{
    if (on)
        bfree_guest_qrc_alloc_scope_enter();
    else
        bfree_guest_qrc_alloc_scope_leave();
}

static unsigned g_malloc_fail_diag;

static void bfree_guest_fill_auxv_tables(void);

extern "C" int bfree_guest_ensure_fallback_heap(void)
{
    if (bfree_guest_fallback_heap_base
        && bfree_guest_fallback_heap_cap >= BFREE_GUEST_FALLBACK_HEAP_BYTES)
        return 0;
    if (bfree_guest_mmap_fixed_anon((uintptr_t)BFREE_GUEST_FALLBACK_HEAP_VA,
                                    BFREE_GUEST_FALLBACK_HEAP_BYTES) != 0) {
        bfree_guest_serial_lit("[desktop_qt] fallback heap mmap fail\n");
        return -1;
    }
    bfree_guest_fallback_heap_base = (unsigned char *)(uintptr_t)BFREE_GUEST_FALLBACK_HEAP_VA;
    bfree_guest_fallback_heap_cap = BFREE_GUEST_FALLBACK_HEAP_BYTES;
    bfree_guest_fallback_heap_off = 0;
    bfree_guest_serial_lit("[desktop_qt] fallback heap ok va=");
    bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)BFREE_GUEST_FALLBACK_HEAP_VA);
    bfree_guest_serial_lit("\n");
    return 0;
}

/* Map ctor stack + fallback before QGuiApplication (defer bump mmap to QML hybrid). */
static void bfree_guest_preflight_guest_arenas(void)
{
    uintptr_t ctor_top = 0;

    (void)bfree_guest_mmap_ctor_stack_at(BFREE_GUEST_CTOR_MMAP_STACK_BYTES, &ctor_top);
    if (bfree_guest_ensure_fallback_heap() != 0)
        bfree_guest_serial_lit("[desktop_qt] preflight: fallback heap mmap fail\n");
    bfree_guest_serial_lit("[desktop_qt] guest arenas preflight ok\n");
}

extern "C" void bfree_guest_preflight_arenas(void)
{
    bfree_guest_preflight_guest_arenas();
}

static int bfree_guest_ptr_is_mmap_fallback(void *p)
{
    uintptr_t u = (uintptr_t)p;

    if (!p || !bfree_guest_fallback_heap_base)
        return 0;
    return u >= (uintptr_t)bfree_guest_fallback_heap_base + sizeof(size_t)
        && u < (uintptr_t)bfree_guest_fallback_heap_base + bfree_guest_fallback_heap_cap;
}

/* Fallback bump blocks store size before the user pointer; Qt may pass interior ptrs. */
static int bfree_guest_fallback_resolve_block(void *p, void **hdr_out, size_t *sz_out)
{
    unsigned char *base;
    size_t off;
    uintptr_t u;

    if (!p || !bfree_guest_fallback_heap_base || !hdr_out || !sz_out)
        return 0;
    u = (uintptr_t)p;
    base = bfree_guest_fallback_heap_base;
    if (u < (uintptr_t)base + sizeof(size_t)
        || u >= (uintptr_t)base + bfree_guest_fallback_heap_off)
        return 0;

    off = 0;
    while (off < bfree_guest_fallback_heap_off) {
        const size_t sz = *(size_t *)(base + off);
        void *usr = base + off + sizeof(size_t);

        if (!sz)
            break;
        if ((void *)u >= usr && u < (uintptr_t)usr + sz) {
            *hdr_out = usr;
            *sz_out = sz;
            return 1;
        }
        off += (sz + sizeof(size_t) + 15U) & ~15U;
    }
    return 0;
}

static int bfree_guest_ptr_near_exec_stack(uintptr_t u)
{
    uintptr_t rsp = g_ctor_stack_ctx.saved_rsp;

    if (rsp == 0)
        return 0;
    if (u >= rsp - (2U << 20) && u <= rsp + 256U)
        return 1;
    return 0;
}

static int bfree_guest_malloc_ptr_sane(void *p)
{
    uintptr_t u;

    if (!p)
        return 1;
    u = (uintptr_t)p;
    if (bfree_guest_ptr_near_exec_stack(u))
        return 0;
    return 1;
}

static void *bfree_guest_mmap_fallback_alloc(size_t n)
{
    void *p;
    static unsigned g_mmap_fallback_diag;

    if (bfree_guest_ensure_fallback_heap() != 0)
        return 0;
    p = bfree_guest_bump_alloc_raw(bfree_guest_fallback_heap_base, bfree_guest_fallback_heap_cap,
                                   &bfree_guest_fallback_heap_off, n);
    if (!p)
        return 0;
    if (!bfree_guest_malloc_ptr_sane(p)) {
        bfree_guest_serial_lit("[desktop_qt] fallback alloc bad ptr=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)p);
        bfree_guest_serial_lit("\n");
        return 0;
    }
    if (g_mmap_fallback_diag < (bfree_guest_qv4_mmap_active ? 24u : 4u)) {
        ++g_mmap_fallback_diag;
        bfree_guest_serial_lit("[desktop_qt] fallback heap alloc n=");
        bfree_guest_serial_hex_u64((uint64_t)n);
        bfree_guest_serial_lit("\n");
    }
    if (!bfree_guest_on_mmap_ctor_stack)
        memset(p, 0, n);
    return p;
}

static size_t bfree_guest_fallback_user_size(void *p)
{
    void *hdr;
    size_t sz;

    if (bfree_guest_fallback_resolve_block(p, &hdr, &sz))
        return sz;
    return 0;
}

static void *bfree_guest_fallback_realloc(void *old, size_t n)
{
    void *hdr;
    size_t old_sz;
    void *p;

    if (!old)
        return bfree_guest_mmap_fallback_alloc(n);
    if (n == 0)
        return 0;
    if (bfree_guest_fallback_resolve_block(old, &hdr, &old_sz)) {
        p = bfree_guest_mmap_fallback_alloc(n);
        if (p && old_sz)
            memcpy(p, hdr, old_sz < n ? old_sz : n);
        return p;
    }
    if (bfree_guest_ptr_in_bump_heap(old))
        old_sz = bfree_guest_bump_user_size(old);
    else if (bfree_guest_musl_malloc_ready)
        old_sz = bfree_guest_malloc_usable_size(old);
    else
        old_sz = 0;
    hdr = old;
    p = bfree_guest_mmap_fallback_alloc(n);
    if (p && old_sz)
        memcpy(p, hdr, old_sz < n ? old_sz : n);
    return p;
}

/* STAGE 3 QGuiApplication: musl alloc_slot #PF CR2=8 on first __real_malloc — route
 * wrapped malloc through 128 MiB fallback @0x19000000 on the mmap ctor stack. */
static int bfree_guest_qgui_mmap_stack_alloc_mode(void)
{
    return bfree_guest_on_mmap_ctor_stack && bfree_guest_musl_malloc_ready
        && !bfree_guest_ctor_bump_mode && !bfree_guest_qv4_mmap_active && !bfree_guest_qrc_alloc_scope;
}

static void *bfree_guest_qrc_qv4_small_alloc(size_t n)
{
    void *p = 0;
    void *qv4;

    qv4 = bfree_guest_mmap_qv4_arena(n);
    if (qv4 != (void *)-1L)
        p = qv4;
    if (!p)
        p = bfree_guest_bump_alloc(n);
    if (!p && bfree_guest_ctor_bump_hybrid && bfree_guest_ctor_bump_base)
        p = bfree_guest_bump_alloc_raw(bfree_guest_ctor_bump_base, bfree_guest_ctor_bump_cap,
                                         &bfree_guest_ctor_bump_off, n);
    return p;
}

extern "C" void *__wrap_malloc(size_t n)
{
    void *p = 0;

    if (bfree_guest_qgui_mmap_stack_alloc_mode()) {
        p = bfree_guest_mmap_fallback_alloc(n);
        if (!p)
            p = bfree_guest_bump_alloc(n);
        bfree_guest_trace_alloc((unsigned long)n, p);
        return p;
    }

    if (bfree_guest_qrc_alloc_scope || bfree_guest_qv4_mmap_active) {
        static unsigned g_qv4_malloc_diag;

        if (bfree_guest_qv4_mmap_active && n >= BFREE_GUEST_QV4_LARGE_THRESHOLD)
            p = bfree_guest_qv4_large_mmap(n);
        if (!p && bfree_guest_qv4_mmap_active)
            p = bfree_guest_mmap_fallback_alloc(n);
        if (!p && bfree_guest_qrc_alloc_scope)
            p = bfree_guest_qrc_qv4_small_alloc(n);
        if (!p && bfree_guest_qv4_mmap_active)
            p = bfree_guest_bump_alloc(n);
        if (!p && bfree_guest_ctor_bump_hybrid && bfree_guest_ctor_bump_base)
            p = bfree_guest_bump_alloc_raw(bfree_guest_ctor_bump_base, bfree_guest_ctor_bump_cap,
                                             &bfree_guest_ctor_bump_off, n);
        if (!p && bfree_guest_qv4_mmap_active) {
            void *qv4 = bfree_guest_mmap_qv4_arena(n);
            if (qv4 != (void *)-1L)
                p = qv4;
        }
        /* Never __real_malloc in qrc/qv4 — musl chunks corrupt exec-stack callers. */
        if (p && !bfree_guest_malloc_ptr_sane(p)) {
            bfree_guest_serial_lit("[desktop_qt] malloc bad ptr n=");
            bfree_guest_serial_hex_u64((uint64_t)n);
            bfree_guest_serial_lit(" p=");
            bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)p);
            bfree_guest_serial_lit("\n");
            p = 0;
        }
        if (bfree_guest_qv4_mmap_active && g_qv4_malloc_diag < 48u) {
            ++g_qv4_malloc_diag;
            bfree_guest_serial_lit("[qv4] malloc n=");
            bfree_guest_serial_hex_u64((uint64_t)n);
            bfree_guest_serial_lit(" p=");
            bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)p);
            bfree_guest_serial_lit("\n");
        }
        if (!p && g_malloc_fail_diag < 12u) {
            ++g_malloc_fail_diag;
            bfree_guest_serial_lit("[desktop_qt] malloc fail n=");
            bfree_guest_serial_hex_u64((uint64_t)n);
            bfree_guest_serial_lit(" qv4=1\n");
        }
        return p;
    }

    if (bfree_guest_ctor_bump_mode && bfree_guest_ctor_bump_hybrid && bfree_guest_musl_malloc_ready) {
        p = bfree_guest_bump_alloc(n);
        if (!p && bfree_guest_ctor_bump_base)
            p = bfree_guest_bump_alloc_raw(bfree_guest_ctor_bump_base, bfree_guest_ctor_bump_cap,
                                           &bfree_guest_ctor_bump_off, n);
        if (!p)
            p = bfree_guest_mmap_fallback_alloc(n);
        /* mmap session: never musl — mixed allocators corrupt QVariant/QString dtor. */
        if (!p && !bfree_guest_on_mmap_ctor_stack) {
            bfree_guest_fill_auxv_tables();
            p = __real_malloc(n);
        }
        if (!p)
            p = bfree_guest_mmap_fallback_alloc(n);
        bfree_guest_trace_alloc((unsigned long)n, p);
        return p;
    }
    if (bfree_guest_ctor_bump_mode) {
        p = bfree_guest_bump_alloc(n);
        if (p || !bfree_guest_ctor_bump_hybrid || !bfree_guest_musl_malloc_ready) {
            bfree_guest_trace_alloc((unsigned long)n, p);
            return p;
        }
    }
    if (bfree_guest_musl_malloc_ready && !bfree_guest_on_mmap_ctor_stack) {
        bfree_guest_fill_auxv_tables();
        p = __real_malloc(n);
        if (!p)
            p = bfree_guest_mmap_fallback_alloc(n);
    } else if (bfree_guest_on_mmap_ctor_stack) {
        p = bfree_guest_mmap_fallback_alloc(n);
        if (!p)
            p = bfree_guest_bump_alloc(n);
    } else {
        p = bfree_guest_bump_alloc(n);
    }
    if (!p && !bfree_guest_ctor_bump_mode && n <= 0x10000U) {
        p = bfree_guest_bump_alloc_raw(bfree_guest_bump_heap, sizeof(bfree_guest_bump_heap),
                                         &bfree_guest_bump_off, n);
    }
    if (!p)
        p = bfree_guest_mmap_fallback_alloc(n);
    if (!p && g_malloc_fail_diag < 12u) {
        ++g_malloc_fail_diag;
        bfree_guest_serial_lit("[desktop_qt] malloc fail n=");
        bfree_guest_serial_hex_u64((uint64_t)n);
        bfree_guest_serial_lit(" musl=");
        bfree_guest_serial_hex_u64((uint64_t)(unsigned)bfree_guest_musl_malloc_ready);
        bfree_guest_serial_lit("\n");
    }
    bfree_guest_trace_alloc((unsigned long)n, p);
    return p;
}

extern "C" _Unwind_Reason_Code __wrap__Unwind_RaiseException(_Unwind_Exception *exc)
{
    (void)exc;
    bfree_guest_serial_lit("[desktop_qt] Unwind_RaiseException blocked (no EH)\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
    return _URC_FATAL_PHASE1_ERROR;
}

extern "C" void *__wrap_calloc(size_t n, size_t sz)
{
    size_t total;
    void *p = 0;

    if (n != 0 && sz > (size_t)-1 / n) {
        errno = ENOMEM;
        return 0;
    }
    total = n * sz;

    if (bfree_guest_qrc_alloc_scope || bfree_guest_qv4_mmap_active) {
        p = bfree_guest_mmap_fallback_alloc(total);
        if (p && total)
            memset(p, 0, total);
        return p;
    }

    /* Never __real_calloc: musl calloc calls wrapped malloc then __malloc_allzerop
     * on the pointer; bump/fallback blocks are not musl chunks and GP-fault there. */
    p = __wrap_malloc(total);
    if (p && total)
        memset(p, 0, total);
    return p;
}

extern "C" void *__wrap_realloc(void *old, size_t n)
{
    if (bfree_guest_qrc_alloc_scope || bfree_guest_qv4_mmap_active)
        return bfree_guest_fallback_realloc(old, n);
    if (bfree_guest_ctor_bump_mode)
        return bfree_guest_ctor_bump_hybrid ? bfree_guest_hybrid_realloc(old, n)
                                            : bfree_guest_bump_realloc(old, n);
    if (bfree_guest_on_mmap_ctor_stack && bfree_guest_musl_malloc_ready) {
        if (!bfree_guest_ctor_bump_mode)
            return bfree_guest_fallback_realloc(old, n);
        return bfree_guest_shallow_musl_realloc(old, n);
    }
    if (bfree_guest_musl_malloc_ready)
        return __real_realloc(old, n);
    return bfree_guest_bump_realloc(old, n);
}

extern "C" void __wrap_free(void *p)
{
    if (!p)
        return;
    if (bfree_guest_bump_user_size(p) != 0)
        return;
    if (bfree_guest_ptr_is_mmap_fallback(p))
        return;
    if (bfree_guest_musl_malloc_ready)
        __real_free(p);
}

extern "C" void *__wrap___libc_malloc(size_t n)
{
    return __wrap_malloc(n);
}
extern "C" void *__wrap___libc_calloc(size_t n, size_t sz)
{
    return __wrap_calloc(n, sz);
}
extern "C" void *__wrap___libc_realloc(void *old, size_t n)
{
    return __wrap_realloc(old, n);
}
extern "C" void __wrap___libc_free(void *p)
{
    __wrap_free(p);
}

extern "C" void *__wrap__Znwm(size_t n)
{
    return __wrap_malloc(n);
}

extern "C" void *__wrap__Znam(size_t n)
{
    return __wrap_malloc(n);
}

extern "C" void *__wrap_aligned_alloc(size_t align, size_t size)
{
    uintptr_t raw;
    uintptr_t aligned;

    if (size == 0)
        return 0;
    if (align < sizeof(void *))
        align = sizeof(void *);
    if ((align & (align - 1)) != 0)
        return 0;
    raw = (uintptr_t)__wrap_malloc(size + align - 1);
    if (!raw)
        return 0;
    aligned = (raw + align - 1) & ~(uintptr_t)(align - 1);
    return (void *)aligned;
}

extern "C" int __wrap_posix_memalign(void **memptr, size_t align, size_t size)
{
    void *p;

    if (!memptr)
        return EINVAL;
    p = __wrap_aligned_alloc(align, size);
    if (!p)
        return ENOMEM;
    *memptr = p;
    return 0;
}

/* B-Free guest has no Linux vDSO; musl __vdsosym(NULL) → #PF at QGuiApplication. */
extern "C" void *__vdsosym(const char *ver, const char *name)
{
    (void)ver;
    (void)name;
    return 0;
}

/* Prebuilt libc.a __clock_gettime uses vDSO; override before archive link (guest_link_compat.o). */
#define BFREE_SYS_CLOCK_GETTIME 228L
#define BFREE_SYS_GETTIMEOFDAY  96L

extern int bfree_guest_errno_storage;

extern "C" int __clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(BFREE_SYS_CLOCK_GETTIME), "D"(clk_id), "S"(tp)
                     : "rcx", "r11", "r8", "r9", "r10", "memory");
    if (ret < 0) {
        bfree_guest_errno_storage = (int)(-ret);
        return -1;
    }
    return 0;
}

extern "C" int clock_gettime(clockid_t clk_id, struct timespec *tp)
    __attribute__((alias("__clock_gettime")));

extern "C" int gettimeofday(struct timeval *tv, void *tz)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(BFREE_SYS_GETTIMEOFDAY), "D"(tv), "S"(tz)
                     : "rcx", "r11", "r8", "r9", "r10", "memory");
    if (ret < 0) {
        bfree_guest_errno_storage = (int)(-ret);
        return -1;
    }
    return (int)ret;
}

extern "C" void bfree_guest_enable_main_bump_arena(void)
{
    if (!bfree_guest_ctor_bump_base && bfree_guest_mmap_ctor_bump_arena() != 0) {
        bfree_guest_serial_lit("[desktop_qt] main bump arena mmap fail\n");
        return;
    }
    bfree_guest_ctor_bump_mode = 1;
    bfree_guest_ctor_bump_hybrid = 0;
    /* Keep musl_malloc_ready=1 so ctor sessions pick the 80 MiB mmap stack (not 8 MiB .bss). */
    bfree_guest_serial_lit("[desktop_qt] main heap: ctor bump arena off=");
    bfree_guest_serial_hex_u64((uint64_t)bfree_guest_ctor_bump_off);
    bfree_guest_serial_lit("\n");
}

/* musl setenv/getenv iterate environ; freestanding guest uses static KEY=value rows. */
static char bfree_guest_env_qpa[] = "QT_QPA_PLATFORM=bfree";
static char bfree_guest_env_quick[] = "QT_QUICK_BACKEND=software";
static char bfree_guest_env_noft[] = "QT_NO_FT_LIB=1";
static char bfree_guest_env_theme[] = "QT_QPA_PLATFORMTHEME=";
static char bfree_guest_env_qmlcache[] = "QML_DISABLE_DISK_CACHE=1";
static char bfree_guest_env_fs[] = "BFREE_DESKTOP_PSEUDO_FULLSCREEN=1";
static char bfree_guest_env_dbgplug[] = "QT_DEBUG_PLUGINS=0";
static char bfree_guest_env_logrules[] = "QT_LOGGING_RULES=qt.qpa.*=false";
static char bfree_guest_env_lcall[] = "LC_ALL=C";
static char bfree_guest_env_home[] = "HOME=/";
static char bfree_guest_env_tmp[] = "TMPDIR=/tmp";
static char bfree_guest_env_xdg[] = "XDG_CONFIG_HOME=/";
static char bfree_guest_env_msgpat[] = "QT_MESSAGE_PATTERN=%m";
static char bfree_guest_env_nofatal[] = "QT_FATAL_WARNINGS=0";
static char bfree_guest_env_nologini[] = "QT_LOGGING_CONF=";
static char bfree_guest_env_noglib[] = "QT_NO_GLIB=1";
static char bfree_guest_env_path[] = "PATH=/";
static char bfree_guest_env_xdgrun[] = "XDG_RUNTIME_DIR=/tmp";
static char bfree_guest_env_qv4js[] = "QV4_JS_MAX_STACK_SIZE=2097152";
static char bfree_guest_env_qv4gc[] = "QV4_GC_MAX_STACK_SIZE=1048576";
static char bfree_guest_env_qv4interp[] = "QV4_FORCE_INTERPRETER=1";
static char bfree_guest_env_fixedlocale[] = "BFREE_GUEST_FIXED_LOCALE=1";

extern "C" {
char *bfree_guest_environ_storage[25];
char **environ = bfree_guest_environ_storage;
char **__environ = bfree_guest_environ_storage;
}

extern "C" void bfree_guest_install_static_env(void)
{
    bfree_guest_environ_storage[0] = bfree_guest_env_qpa;
    bfree_guest_environ_storage[1] = bfree_guest_env_quick;
    bfree_guest_environ_storage[2] = bfree_guest_env_noft;
    bfree_guest_environ_storage[3] = bfree_guest_env_theme;
    bfree_guest_environ_storage[4] = bfree_guest_env_qmlcache;
    bfree_guest_environ_storage[5] = bfree_guest_env_fs;
    bfree_guest_environ_storage[6] = bfree_guest_env_dbgplug;
    bfree_guest_environ_storage[7] = bfree_guest_env_logrules;
    bfree_guest_environ_storage[8] = bfree_guest_env_lcall;
    bfree_guest_environ_storage[9] = bfree_guest_env_home;
    bfree_guest_environ_storage[10] = bfree_guest_env_tmp;
    bfree_guest_environ_storage[11] = bfree_guest_env_xdg;
    bfree_guest_environ_storage[12] = bfree_guest_env_msgpat;
    bfree_guest_environ_storage[13] = bfree_guest_env_nofatal;
    bfree_guest_environ_storage[14] = bfree_guest_env_nologini;
    bfree_guest_environ_storage[15] = bfree_guest_env_noglib;
    bfree_guest_environ_storage[16] = bfree_guest_env_path;
    bfree_guest_environ_storage[17] = bfree_guest_env_xdgrun;
    bfree_guest_environ_storage[18] = bfree_guest_env_qv4js;
    bfree_guest_environ_storage[19] = bfree_guest_env_qv4gc;
    bfree_guest_environ_storage[20] = bfree_guest_env_qv4interp;
    bfree_guest_environ_storage[21] = bfree_guest_env_fixedlocale;
    bfree_guest_environ_storage[22] = nullptr;
    /* libc.a may win the link-time environ symbol; patch both at runtime. */
    environ = bfree_guest_environ_storage;
    __environ = bfree_guest_environ_storage;
}

static char *bfree_guest_getenv_static(const char *name)
{
    size_t n;
    char *const *p;

    if (!name)
        return 0;
    n = strlen(name);
    for (p = bfree_guest_environ_storage; *p; ++p) {
        if (strncmp(*p, name, n) == 0 && (*p)[n] == '=')
            return *p + n + 1;
    }
    return 0;
}

extern "C" char *__wrap_getenv(const char *name)
{
    bfree_guest_install_static_env();
    return bfree_guest_getenv_static(name);
}

/* Install static env before Qt init_array so QMessagePattern never reads host garbage. */
static void bfree_guest_early_env_init(void) __attribute__((constructor(101)));

static void bfree_guest_early_env_init(void)
{
    bfree_guest_install_static_env();
}

extern "C" char *realpath(const char *path, char *resolved);

extern "C" void __real_abort(void) __attribute__((noreturn));

extern "C" void __wrap_abort(void)
{
    void *ra = __builtin_return_address(0);
    bfree_guest_serial_lit("[desktop_qt] abort() ra=");
    bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)ra);
    bfree_guest_serial_lit("\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
}

extern "C" uid_t __wrap_getuid(void)
{
    return 0;
}

extern "C" uid_t __wrap_geteuid(void)
{
    return 0;
}

/* Qt QThreadData uses pthread_setspecific in QCoreApplicationPrivate ctor; musl aborts on guest TLS. */
#define BFREE_GUEST_PTHREAD_KEYS 64
static void *g_guest_pthread_keys[BFREE_GUEST_PTHREAD_KEYS];
static void (*g_guest_pthread_dtors[BFREE_GUEST_PTHREAD_KEYS])(void *);

extern "C" int __wrap_pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    static pthread_key_t g_next_key = 1;
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("pthread_key_create");
    if (!key)
        return EINVAL;
    if (g_next_key >= BFREE_GUEST_PTHREAD_KEYS)
        return EAGAIN;
    g_guest_pthread_dtors[g_next_key] = destructor;
    *key = g_next_key++;
    return 0;
}

extern "C" int __wrap_pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key <= 0 || key >= BFREE_GUEST_PTHREAD_KEYS)
        return EINVAL;
    g_guest_pthread_keys[key] = (void *)value;
    return 0;
}

extern "C" void *__wrap_pthread_getspecific(pthread_key_t key)
{
    if (key <= 0 || key >= BFREE_GUEST_PTHREAD_KEYS)
        return 0;
    return g_guest_pthread_keys[key];
}

extern "C" int __wrap_pthread_key_delete(pthread_key_t key)
{
    if (key <= 0 || key >= BFREE_GUEST_PTHREAD_KEYS)
        return EINVAL;
    g_guest_pthread_keys[key] = 0;
    g_guest_pthread_dtors[key] = 0;
    return 0;
}

extern "C" pthread_t __wrap_pthread_self(void)
{
    return (pthread_t)(uintptr_t)&g_pthread_main_obj;
}

extern "C" void bfree_guest_enable_musl_malloc(void);

extern "C" void bfree_guest_preflight_musl_heap(void)
{
    long brk_cur;
    long brk_new;
    unsigned i;

    /* Shallow path only: large warmup on mmap ctor stack recurses past 256 MiB. */
    bfree_guest_ctor_bump_mode = 0;
    bfree_guest_ctor_bump_hybrid = 0;
    bfree_guest_enable_musl_malloc();
    bfree_guest_fill_auxv_tables();
    brk_cur = syscall(12L, 0L);
    /* Grow brk below ctor mmap (0x08000000) so musl avoids identity PTE band. */
    for (i = 0; i < 24u; ++i) {
        if (brk_cur >= 0x07000000L)
            break;
        brk_new = syscall(12L, brk_cur + 0x200000L);
        if (brk_new == brk_cur + 0x200000L)
            brk_cur = brk_new;
        else
            break;
    }
    bfree_guest_serial_lit("[desktop_qt] musl malloc preflight OK brk=");
    bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)brk_cur);
    bfree_guest_serial_lit("\n");
    bfree_guest_preflight_guest_arenas();
}

extern "C" void bfree_guest_preflight_brk_only(void)
{
    long brk_cur;
    long brk_new;
    unsigned i;

    bfree_guest_ctor_bump_mode = 0;
    bfree_guest_ctor_bump_hybrid = 0;
    bfree_guest_enable_musl_malloc();
    bfree_guest_fill_auxv_tables();
    brk_cur = syscall(12L, 0L);
    for (i = 0; i < 24u; ++i) {
        if (brk_cur >= 0x07000000L)
            break;
        brk_new = syscall(12L, brk_cur + 0x200000L);
        if (brk_new == brk_cur + 0x200000L)
            brk_cur = brk_new;
        else
            break;
    }
    bfree_guest_serial_lit("[desktop_qt] musl brk preflight OK brk=");
    bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)brk_cur);
    bfree_guest_serial_lit("\n");
}

extern "C" int __wrap_pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (once_control && *once_control == 0) {
        static unsigned g_once_qv4_diag;
        if (bfree_guest_qv4_mmap_active) {
            if (g_once_qv4_diag < 8u) {
                ++g_once_qv4_diag;
                bfree_guest_qv4_trace_tag("pthread_once");
            }
            bfree_pthread_pump_qv4();
        }
        *once_control = 1;
        if (init_routine) {
            const uintptr_t ir = (uintptr_t)(void *)init_routine;
            if (!bfree_guest_code_entry_looks_valid(ir)) {
                bfree_guest_serial_lit("[desktop_qt] pthread_once bad init_routine=");
                bfree_guest_serial_hex_u64((uint64_t)ir);
                bfree_guest_serial_lit("\n");
            } else {
                init_routine();
                if (bfree_guest_qv4_mmap_active)
                    bfree_pthread_pump_qv4();
            }
        }
    }
    return 0;
}

extern "C" char *__wrap_setlocale(int category, const char *locale)
{
    static char c_locale[] = "C";
    (void)category;
    (void)locale;
    return c_locale;
}

extern "C" char *__wrap_nl_langinfo(int item)
{
    (void)item;
    return (char *)"UTF-8";
}

extern "C" char *__real_getcwd(char *buf, size_t size);

extern "C" char *__wrap_getcwd(char *buf, size_t size)
{
    char *ret = __real_getcwd(buf, size);
    static unsigned g_getcwd_diag;
    if (g_getcwd_diag < 4u) {
        ++g_getcwd_diag;
        bfree_guest_serial_lit("[desktop_qt] getcwd ok\n");
    }
    return ret;
}

extern "C" ssize_t __real_readlink(const char *path, char *buf, size_t bufsiz);

extern "C" ssize_t __wrap_readlink(const char *path, char *buf, size_t bufsiz)
{
    static unsigned g_readlink_diag;
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("readlink");
    if (g_readlink_diag < 8u) {
        ++g_readlink_diag;
        bfree_guest_serial_lit("[desktop_qt] readlink\n");
    }
    if (path && strcmp(path, "/proc/self/exe") == 0) {
        const char *exe = "/desktop";
        size_t n = strlen(exe);
        if (n + 1u > bufsiz) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(buf, exe, n);
        buf[n] = '\0';
        return (ssize_t)n;
    }
    errno = EINVAL;
    return -1;
}

extern "C" int __real_gettimeofday(struct timeval *tv, struct timezone *tz);

extern "C" int __wrap_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    static unsigned g_gettimeofday_diag;
    if (!bfree_guest_on_mmap_ctor_stack || bfree_guest_qv4_mmap_active) {
        if (bfree_guest_qv4_mmap_active)
            bfree_pthread_pump_qv4();
        else
            bfree_pthread_run_pending();
    }
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("gettimeofday");
    if (g_gettimeofday_diag < 32u) {
        ++g_gettimeofday_diag;
        bfree_guest_serial_lit("[desktop_qt] gettimeofday\n");
    }
    return __real_gettimeofday(tv, tz);
}

extern "C" int __real_clock_gettime(clockid_t clk_id, struct timespec *tp);

extern "C" int __wrap_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    static unsigned g_clock_gettime_diag;
    if (bfree_guest_qv4_mmap_active) {
        bfree_guest_qv4_trace_tag("clock_gettime");
        bfree_pthread_pump_qv4();
    }
    if (g_clock_gettime_diag < 16u) {
        ++g_clock_gettime_diag;
        bfree_guest_serial_lit("[desktop_qt] clock_gettime\n");
    }
    return __real_clock_gettime(clk_id, tp);
}

extern "C" int __real_nanosleep(const struct timespec *req, struct timespec *rem);

extern "C" int __wrap_nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (bfree_guest_qv4_mmap_active) {
        static unsigned g_qv4_nanosleep_diag;
        if (g_qv4_nanosleep_diag < 48u) {
            ++g_qv4_nanosleep_diag;
            bfree_guest_qv4_trace_tag("nanosleep");
            if (req) {
                bfree_guest_serial_lit("[qv4] nanosleep sec=");
                bfree_guest_serial_hex_u64((uint64_t)(uint64_t)req->tv_sec);
                bfree_guest_serial_lit(" nsec=");
                bfree_guest_serial_hex_u64((uint64_t)(uint64_t)req->tv_nsec);
                bfree_guest_serial_lit("\n");
            }
        }
        bfree_pthread_pump_qv4();
        if (rem)
            *rem = {0, 0};
        return 0;
    }
    return __real_nanosleep(req, rem);
}

extern "C" int __real_stat(const char *path, struct stat *st);

static void bfree_guest_stat_fill_dir(struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = (mode_t)(S_IFDIR | 0555);
    st->st_nlink = 2;
    st->st_size = 4096;
}

static void bfree_guest_stat_fill_reg(struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = (mode_t)(S_IFREG | 0555);
    st->st_nlink = 1;
    st->st_size = 42u * 1024u * 1024u;
}

extern "C" int __wrap_stat(const char *path, struct stat *st)
{
    static unsigned g_stat_diag;
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("stat");
    if (g_stat_diag < 16u) {
        ++g_stat_diag;
        bfree_guest_serial_lit("[desktop_qt] stat\n");
    }
    if (st && path) {
        if (strcmp(path, "/") == 0) {
            bfree_guest_stat_fill_dir(st);
            return 0;
        }
        if (strcmp(path, "/desktop") == 0) {
            bfree_guest_stat_fill_reg(st);
            return 0;
        }
        memset(st, 0, sizeof(*st));
    }
    errno = ENOENT;
    return -1;
}

extern "C" int __real_lstat(const char *path, struct stat *st);

extern "C" int __wrap_lstat(const char *path, struct stat *st)
{
    static unsigned g_lstat_diag;
    if (bfree_guest_qv4_mmap_active)
        bfree_guest_qv4_trace_tag("lstat");
    if (g_lstat_diag < 16u) {
        ++g_lstat_diag;
        bfree_guest_serial_lit("[desktop_qt] lstat\n");
    }
    if (st && path) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/desktop") == 0) {
            if (strcmp(path, "/") == 0)
                bfree_guest_stat_fill_dir(st);
            else
                bfree_guest_stat_fill_reg(st);
            return 0;
        }
        memset(st, 0, sizeof(*st));
    }
    errno = ENOENT;
    return -1;
}

extern "C" char *__wrap_realpath(const char *path, char *resolved);

static const char *bfree_guest_realpath_canon(const char *path)
{
    static char realpath_static[4096];
    const char *canon;
    size_t n;

    if (!path) {
        errno = EINVAL;
        return 0;
    }
    if (strcmp(path, "/") == 0)
        canon = "/";
    else if (strcmp(path, "/proc/self/exe") == 0 || strcmp(path, "/desktop") == 0)
        canon = "/desktop";
    else if (strncmp(path, "/root/", 6) == 0 || strstr(path, "bfree-qt") != 0
             || strstr(path, "qtbase") != 0 || strstr(path, "/usr/") != 0
             || strstr(path, "/opt/") != 0)
        canon = "/desktop";
    else if (path[0] != '/')
        canon = "/";
    else
        canon = "/desktop";

    n = strlen(canon);
    if (n == 0) {
        errno = ENOENT;
        return 0;
    }
    if (n >= sizeof(realpath_static)) {
        errno = ENAMETOOLONG;
        return 0;
    }
    memcpy(realpath_static, canon, n + 1u);
    return realpath_static;
}

static void bfree_guest_log_path_prefix(const char *tag, const char *path)
{
    char line[80];
    unsigned i = 0;
    unsigned ti = 0;
    unsigned pi = 0;

    while (tag && tag[ti] != '\0' && i + 1u < sizeof(line))
        line[i++] = tag[ti++];
    if (path) {
        while (path[pi] != '\0' && pi < 48u && i + 1u < sizeof(line))
            line[i++] = path[pi++];
    }
    line[i++] = '\n';
    line[i] = '\0';
    bfree_guest_serial_lit(line);
}

extern "C" char *__wrap_realpath(const char *path, char *resolved)
{
    static unsigned g_realpath_diag;
    const char *canon;
    size_t n;

    if (bfree_guest_qv4_mmap_active && g_realpath_diag < 8u) {
        ++g_realpath_diag;
        bfree_guest_qv4_trace_tag("realpath");
        bfree_guest_log_path_prefix("[desktop_qt] realpath path=", path);
    }

    canon = bfree_guest_realpath_canon(path);
    if (!canon)
        return 0;
    if (!resolved)
        return (char *)canon;
    n = strlen(canon);
    /* realpath(3) without _chk: Qt stack buffers are often 128 bytes. */
    if (n + 1u > 128u)
        return (char *)canon;
    memcpy(resolved, canon, n + 1u);
    bfree_guest_serial_step_raw('W');
    return resolved;
}

/* After static plugin init: QGuiApplication on musl heap (bump pointers stay valid below). */
extern "C" void bfree_guest_leave_ctor_bump_alloc(void)
{
    bfree_guest_ctor_bump_mode = 0;
    bfree_guest_ctor_bump_hybrid = 0;
    bfree_guest_musl_malloc_ready = 1;
    bfree_guest_serial_lit("[desktop_qt] ctor bump -> musl\n");
}

static int bfree_guest_preserve_ctor_bump_off;

extern "C" void bfree_guest_refresh_libc_auxv(void);

/* Run fn on the exec stack saved before mmap RSP switch (Qt QGui ctor). */
extern "C" void bfree_guest_with_saved_exec_rsp(void (*fn)(void))
{
    uintptr_t exec_rsp = g_ctor_stack_ctx.saved_rsp;
    uintptr_t cur_rsp = 0;

    if (!fn)
        return;
    if (exec_rsp == 0) {
        fn();
        return;
    }
    __asm__ volatile("mov %%rsp, %0" : "=r"(cur_rsp));
    bfree_guest_switch_rsp(exec_rsp);
    fn();
    bfree_guest_switch_rsp(cur_rsp);
}

static void bfree_guest_run_on_ctor_stack_inner(void (*fn)(void), int bump_policy, int noreturn)
{
    uintptr_t ctor_top = 0;
    uintptr_t saved_rsp = 0;
    uintptr_t verify_rsp = 0;
    void *ctor_stack = 0;
    void *mmap_stack = MAP_FAILED;
    size_t stack_bytes = BFREE_GUEST_CTOR_STACK_BYTES;
    int used_mmap = 0;
    int saved_musl = 0;
    int saved_ctor_bump = 0;
    int saved_ctor_hybrid = 0;
    unsigned char *saved_ctor_bump_base = 0;
    size_t saved_ctor_bump_cap = 0;

    if (!fn)
        return;

    bfree_guest_session_fn_guard = (uintptr_t)(void *)fn;
    g_ctor_stack_ctx.session_fn = fn;

    bfree_guest_serial_lit("[desktop_qt] ctor inner enter pol=");
    bfree_guest_serial_hex_u64((uint64_t)(unsigned)bump_policy);
    bfree_guest_serial_lit(" nr=");
    bfree_guest_serial_hex_u64((uint64_t)(unsigned)noreturn);
    bfree_guest_serial_lit("\n");

    /* bump_policy 0=musl on full 256M stack; 1=plugin bump; 2=QML hybrid 224M+32M bump. */
    if (bfree_guest_musl_malloc_ready || bump_policy != 0) {
        stack_bytes = BFREE_GUEST_CTOR_MMAP_STACK_BYTES;
        mmap_stack = bfree_guest_mmap_ctor_stack_at(stack_bytes, &ctor_top);
        if (mmap_stack && bump_policy == 2) {
            uintptr_t hybrid_top = (uintptr_t)BFREE_GUEST_CTOR_MMAP_VA
                + BFREE_GUEST_CTOR_MMAP_STACK_BYTES_HYBRID - 256U
                - BFREE_GUEST_CTOR_STACK_CALL_BIAS;
            /* Do not switch RSP above a shrunken mapping (hello 32MiB). */
            if (hybrid_top < ctor_top)
                ctor_top = hybrid_top;
        }
        if (mmap_stack) {
            ctor_stack = mmap_stack;
            used_mmap = 1;
        }
    }
    if (!ctor_stack) {
        if (bump_policy != 0) {
            bfree_guest_serial_lit("[desktop_qt] FATAL: mmap ctor stack required (rebuild ISO)\n");
            for (;;) {
                __asm__ volatile("pause" ::: "memory");
            }
        }
        stack_bytes = BFREE_GUEST_CTOR_STACK_BYTES;
        ctor_stack = bfree_guest_alloc_ctor_stack(&ctor_top);
    }
    if (!ctor_stack || ctor_top == 0 || ctor_top < BFREE_GUEST_EXEC_STACK_FLOOR) {
        bfree_guest_serial_lit("[desktop_qt] ctor stack run failed top=");
        bfree_guest_serial_hex_u64((uint64_t)ctor_top);
        bfree_guest_serial_lit("\n");
        return;
    }
    if (bump_policy != 0) {
        bfree_guest_serial_lit(bump_policy == 2 ? "[desktop_qt] ctor alloc: hybrid\n"
                                                : "[desktop_qt] ctor alloc: bump-only\n");
    } else if (used_mmap && bfree_guest_musl_malloc_ready) {
        bfree_guest_serial_lit("[desktop_qt] ctor alloc: fallback heap (mmap stack)\n");
        saved_ctor_bump_base = bfree_guest_ctor_bump_base;
        saved_ctor_bump_cap = bfree_guest_ctor_bump_cap;
        bfree_guest_ctor_bump_base = 0;
        bfree_guest_ctor_bump_cap = 0;
    }
    if (bump_policy != 0) {
        saved_musl = bfree_guest_musl_malloc_ready;
        saved_ctor_bump = bfree_guest_ctor_bump_mode;
        saved_ctor_hybrid = bfree_guest_ctor_bump_hybrid;
        bfree_guest_ctor_bump_mode = 1;
        bfree_guest_ctor_bump_hybrid = (bump_policy == 2) ? 1 : 0;
        if (bump_policy == 1)
            bfree_guest_musl_malloc_ready = 0;
        if (bump_policy == 2 && !bfree_guest_preserve_ctor_bump_off)
            bfree_guest_ctor_bump_off = 0;
        if (bfree_guest_mmap_ctor_bump_arena() != 0) {
            if (bump_policy == 2) {
                bfree_guest_serial_lit("[desktop_qt] ctor bump mmap fail (musl+fallback)\n");
                bfree_guest_ctor_bump_base = 0;
                bfree_guest_ctor_bump_cap = 0;
                bfree_guest_ctor_bump_off = 0;
                (void)bfree_guest_ensure_fallback_heap();
            } else {
                bfree_guest_serial_lit("[desktop_qt] ctor bump mmap fail\n");
                bfree_guest_ctor_bump_mode = saved_ctor_bump;
                bfree_guest_ctor_bump_hybrid = saved_ctor_hybrid;
                bfree_guest_musl_malloc_ready = saved_musl;
                return;
            }
        } else if (bump_policy == 2) {
            (void)bfree_guest_ensure_fallback_heap();
        }
    }
    __asm__ volatile("mov %%rsp, %0" : "=r"(saved_rsp));
    g_ctor_stack_ctx.saved_rsp = saved_rsp;
    g_ctor_stack_ctx.mmap_stack = mmap_stack;
    g_ctor_stack_ctx.stack_bytes = stack_bytes;
    g_ctor_stack_ctx.session_fn = (void (*)(void))bfree_guest_session_fn_guard;
    bfree_guest_on_mmap_ctor_stack = used_mmap;
    bfree_guest_serial_lit("[desktop_qt] ctor stack ");
    if (bump_policy == 2)
        bfree_guest_serial_lit("hybrid ");
    bfree_guest_serial_lit(used_mmap ? "mmap" : "bss");
    if (used_mmap) {
        bfree_guest_serial_lit("@");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)mmap_stack);
    }
    bfree_guest_serial_lit(" rsp ");
    bfree_guest_serial_hex_u64((uint64_t)saved_rsp);
    bfree_guest_serial_lit(" -> ");
    bfree_guest_serial_hex_u64((uint64_t)ctor_top);
    if (bump_policy != 0) {
        bfree_guest_serial_lit(" ctor_bump_off=");
        bfree_guest_serial_hex_u64((uint64_t)bfree_guest_ctor_bump_off);
    }
    bfree_guest_serial_lit("\n");
    if (noreturn) {
        bfree_guest_serial_lit("[desktop_qt] ctor mmap invoke\n");
        bfree_guest_switch_and_invoke_noreturn(ctor_top,
            (void (*)(void))bfree_guest_session_fn_guard);
    }
    bfree_guest_switch_rsp(ctor_top);
    bfree_guest_serial_lit("[desktop_qt] ctor mmap rsp ok\n");
    bfree_guest_serial_lit("[desktop_qt] ctor inner call fn\n");
    bfree_guest_call_saved_session_fn();
    if (noreturn) {
        bfree_guest_serial_lit("[desktop_qt] FATAL: mmap noreturn fn returned\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    bfree_guest_on_mmap_ctor_stack = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(verify_rsp));
    bfree_guest_serial_lit("[desktop_qt] ctor stack leave rsp=");
    bfree_guest_serial_hex_u64((uint64_t)verify_rsp);
    if (bump_policy != 0) {
        bfree_guest_serial_lit(" ctor_bump_end=");
        bfree_guest_serial_hex_u64((uint64_t)bfree_guest_ctor_bump_off);
    }
    bfree_guest_serial_lit("\n");
    bfree_guest_switch_rsp(g_ctor_stack_ctx.saved_rsp);
    if (bump_policy == 0 && saved_ctor_bump_base) {
        bfree_guest_ctor_bump_base = saved_ctor_bump_base;
        bfree_guest_ctor_bump_cap = saved_ctor_bump_cap;
    }
    if (bump_policy != 0) {
        bfree_guest_ctor_bump_mode = saved_ctor_bump;
        bfree_guest_ctor_bump_hybrid = saved_ctor_hybrid;
        bfree_guest_musl_malloc_ready = saved_musl;
    }
    /* Keep mmap cached in bfree_guest_mmap_ctor_stack_at for subsequent plugin/qrc calls. */
}

/* Static plugins: 8 MiB BSS stack + 4 MiB main-bump (not mmap bump at 0x0E000000). */
extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_plugins(void (*fn)(void))
{
    uintptr_t ctor_top = 0;
    uintptr_t saved_rsp = 0;
    void *ctor_stack;
    int saved_musl = 0;
    int saved_ctor_bump = 0;
    int saved_ctor_hybrid = 0;
    size_t saved_ctor_bump_off = 0;

    if (!fn)
        return;
    ctor_stack = bfree_guest_alloc_ctor_stack(&ctor_top);
    if (!ctor_stack || ctor_top == 0) {
        bfree_guest_serial_lit("[desktop_qt] plugin ctor stack failed\n");
        return;
    }
    saved_musl = bfree_guest_musl_malloc_ready;
    saved_ctor_bump = bfree_guest_ctor_bump_mode;
    saved_ctor_hybrid = bfree_guest_ctor_bump_hybrid;
    saved_ctor_bump_off = bfree_guest_ctor_bump_off;
    bfree_guest_ctor_bump_mode = 1;
    bfree_guest_ctor_bump_hybrid = 0;
    bfree_guest_musl_malloc_ready = 0;
    bfree_guest_ctor_bump_base = bfree_guest_bump_heap;
    bfree_guest_ctor_bump_cap = sizeof(bfree_guest_bump_heap);
    bfree_guest_ctor_bump_off = 0;
    bfree_guest_serial_lit("[desktop_qt] plugin ctor bss stack bump\n");
    __asm__ volatile("mov %%rsp, %0" : "=r"(saved_rsp));
    bfree_guest_switch_rsp(ctor_top);
    fn();
    bfree_guest_switch_rsp(saved_rsp);
    bfree_guest_ctor_bump_mode = saved_ctor_bump;
    bfree_guest_ctor_bump_hybrid = saved_ctor_hybrid;
    bfree_guest_musl_malloc_ready = saved_musl;
    bfree_guest_ctor_bump_off = saved_ctor_bump_off;
}

extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack(void (*fn)(void))
{
    bfree_guest_run_on_ctor_stack_inner(fn, 1, 0);
}

extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_deep(void (*fn)(void))
{
    /* Bump-only: musl on switched RSP trips libgcc _Unwind_RaiseException (ud2). */
    bfree_guest_run_on_ctor_stack_inner(fn, 1, 0);
}

extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_musl(void (*fn)(void))
{
    /* mmap stack + musl only (shallow realloc path still blows 96 MiB — prefer hybrid). */
    bfree_guest_run_on_ctor_stack_inner(fn, 0, 0);
}

extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_hybrid(void (*fn)(void))
{
    /* mmap stack + bump (shallow realloc) with musl fallback for large allocs. */
    bfree_guest_run_on_ctor_stack_inner(fn, 2, 0);
}

extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_hybrid_keep_bump(void (*fn)(void))
{
    bfree_guest_preserve_ctor_bump_off = 1;
    bfree_guest_run_on_ctor_stack_inner(fn, 2, 0);
    bfree_guest_preserve_ctor_bump_off = 0;
}

/* STAGE 5 + event loop: stay on 224M hybrid mmap stack (do not restore exec RSP). */
extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_hybrid_noreturn(void (*fn)(void))
{
    bfree_guest_preserve_ctor_bump_off = 1;
    bfree_guest_run_on_ctor_stack_inner(fn, 2, 1);
}

/* Map 256M ctor stack early from main (shallow exec stack) so inner noreturn only switches RSP. */
extern "C" void bfree_guest_preflight_ctor_mmap(void)
{
    uintptr_t top = 0;
    void *m = bfree_guest_mmap_ctor_stack_at(BFREE_GUEST_CTOR_MMAP_STACK_BYTES, &top);

    if (!m || top == 0) {
        bfree_guest_serial_lit("[desktop_qt] preflight ctor mmap fail\n");
        return;
    }
    bfree_guest_serial_lit("[desktop_qt] preflight ctor mmap ok top=");
    bfree_guest_serial_hex_u64((uint64_t)top);
    bfree_guest_serial_lit("\n");
}

/* After bfree_guest_preflight_ctor_mmap: switch RSP only (avoid heavy inner on exec stack). */
extern "C" __attribute__((noinline)) void bfree_guest_enter_preflighted_mmap_noreturn(void (*fn)(void))
{
    uintptr_t top = 0;
    void *m;
    uintptr_t saved_rsp = 0;

    if (!fn || !bfree_guest_fn_ptr_looks_valid(fn)) {
        bfree_guest_serial_lit("[desktop_qt] enter mmap: bad fn=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)fn);
        bfree_guest_serial_lit("\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    bfree_guest_session_fn_guard = (uintptr_t)(void *)fn;
    g_ctor_stack_ctx.session_fn = fn;
    m = bfree_guest_mmap_ctor_stack_at(BFREE_GUEST_CTOR_MMAP_STACK_BYTES, &top);
    if (!m || top == 0) {
        bfree_guest_serial_lit("[desktop_qt] enter mmap: preflight cache miss\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    bfree_guest_ctor_bump_base = 0;
    bfree_guest_ctor_bump_cap = 0;
    bfree_guest_ctor_bump_mode = 0;
    bfree_guest_ctor_bump_hybrid = 0;
    g_ctor_stack_ctx.mmap_stack = m;
    g_ctor_stack_ctx.stack_bytes = BFREE_GUEST_CTOR_MMAP_STACK_BYTES;
    bfree_guest_on_mmap_ctor_stack = 1;
    __asm__ volatile("mov %%rsp, %0" : "=r"(saved_rsp));
    g_ctor_stack_ctx.saved_rsp = saved_rsp;
    g_ctor_stack_ctx.session_fn = (void (*)(void))bfree_guest_session_fn_guard;
    bfree_guest_serial_lit("[desktop_qt] enter preflighted mmap heap, exec rsp stay ");
    bfree_guest_serial_hex_u64((uint64_t)saved_rsp);
    bfree_guest_serial_lit(" -> ");
    bfree_guest_serial_hex_u64((uint64_t)top);
    bfree_guest_serial_lit(" fn=");
    bfree_guest_serial_hex_u64((uint64_t)bfree_guest_session_fn_guard);
    bfree_guest_serial_lit("\n");
    bfree_guest_switch_and_invoke_noreturn(top, (void (*)(void))bfree_guest_session_fn_guard);
}

/* QGui + deferred + QML on one 256M musl mmap stack (never return to exec RSP). */
extern "C" __attribute__((noinline)) void bfree_guest_run_on_ctor_stack_musl_noreturn(void (*fn)(void))
{
    bfree_guest_run_on_ctor_stack_inner(fn, 0, 1);
}

/* Enable hybrid bump arena on current (mmap) stack without another RSP switch. */
extern "C" void bfree_guest_begin_hybrid_alloc(void)
{
    const int fresh = !bfree_guest_ctor_bump_mode;

    bfree_guest_ctor_bump_mode = 1;
    bfree_guest_ctor_bump_hybrid = 1;
    if (fresh)
        bfree_guest_ctor_bump_off = 0;
    if (bfree_guest_mmap_ctor_bump_arena() != 0) {
        bfree_guest_serial_lit("[desktop_qt] hybrid bump fail (musl+fallback)\n");
        bfree_guest_ctor_bump_base = 0;
        bfree_guest_ctor_bump_cap = 0;
        (void)bfree_guest_ensure_fallback_heap();
    } else {
        (void)bfree_guest_ensure_fallback_heap();
    }
}

/* Late .init_array ctors (index >= BFREE_GUEST_INIT_ARRAY_DEFER_FROM) run from guest_main
 * after QGuiApplication — ctor[8] NULL-derefs if run during init_array or pre-QGui. */
static int bfree_guest_is_qqmldebug_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    /* _GLOBAL__sub_I_qqmldebugserviceinterfaces.cpp — index shifts when qrc ctors link in. */
    return b[0] == 0xf3 && b[1] == 0x0f && b[2] == 0x1e && b[3] == 0xfa && b[4] == 0x41 && b[5] == 0x54;
}

static int bfree_guest_is_qthread_unix_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    /* __cxa_atexit stub; safe to run but index shifts — keep signature for logging. */
    return b[0] == 0xf3 && b[1] == 0x0f && b[2] == 0x1e && b[3] == 0xfa && b[4] == 0x48 && b[5] == 0x8d
        && b[6] == 0x15 && b[14] == 0xb6 && b[15] == 0x3b;
}

static int bfree_guest_is_qlibrary_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    /* _GLOBAL__sub_I_qlibrary.cpp — lea rsi disp for qlibraryCleanup_dtor_instance. */
    return b[0] == 0xf3 && b[1] == 0x0f && b[2] == 0x1e && b[3] == 0xfa && b[4] == 0x48 && b[5] == 0x8d
        && b[6] == 0x15 && b[14] == 0x0a && b[15] == 0xce;
}

static int bfree_guest_is_eh_globals_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    return b[0] == 0x55 && b[1] == 0xbe;
}

static int bfree_guest_is_qhttpthreaddelegate_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    /* _GLOBAL__sub_I_qhttpthreaddelegate.cpp — QThreadStorageData ctor hits musl malloc #GP. */
    return b[0] == 0xf3 && b[4] == 0x53 && b[5] == 0x48 && b[6] == 0x8d && b[7] == 0x1d
        && b[14] == 0xad && b[15] == 0xdb;
}

static int bfree_guest_is_qv4vme_moth_ctor(void (*fn)(void))
{
    const unsigned char *b = (const unsigned char *)(void *)fn;
    /* _GLOBAL__sub_I_qv4vme_moth.cpp — QList<Breakpoint> + __cxa_atexit. */
    return b[0] == 0xf3 && b[4] == 0x53 && b[5] == 0x48 && b[6] == 0x8d && b[7] == 0x1d
        && b[0xc] == 0x48 && b[0xd] == 0x8d && b[0xe] == 0x35;
}

static int bfree_guest_should_skip_deferred_ctor(void (*fn)(void), unsigned idx)
{
    const uintptr_t a = (uintptr_t)(void *)fn;

    if (!bfree_guest_ctor_ptr_looks_valid(fn))
        return 1;
    if (bfree_guest_is_qrc_ctor(fn) || bfree_guest_is_qqmldebug_ctor(fn) || bfree_guest_is_qthread_unix_ctor(fn)
        || bfree_guest_is_qlibrary_ctor(fn) || bfree_guest_is_eh_globals_ctor(fn)
        || bfree_guest_is_qhttpthreaddelegate_ctor(fn))
        return 1;
    if (idx == 8u || idx == 0x11u || idx == 0x14u || idx == 0x17u || idx == 0x19u || idx == 0x1bu || idx == 0x1cu)
        return 1;
    /* Late libstdc++ ctors from index 0x1f (future/system_error/pmr/ctype copies): musl malloc #GP. */
    if (idx >= 0x1fu)
        return 1;
    /* Fallback: rodata/libstdc++ band (above .text); do not include .text tail <0x35f2000. */
    if (a >= 0x35f2000u && a <= 0x3610000u)
        return 1;
    return 0;
}

extern "C" void bfree_guest_run_deferred_init_array_ctors(void)
{
    uintptr_t ctor_top = 0;
    uintptr_t saved_rsp = 0;
    void *mmap_stack = 0;
    int saved_musl;
    int saved_ctor_bump;
    int saved_ctor_hybrid;
    unsigned i;
    unsigned n;

    if (g_deferred_ctor_count == 0) {
        bfree_guest_serial_lit("[desktop_qt] deferred ctors: none\n");
        return;
    }
    n = g_deferred_ctor_count;
    if (n > BFREE_GUEST_DEFERRED_CTORS_MAX) {
        bfree_guest_serial_lit("[desktop_qt] deferred ctors: count clamped\n");
        n = BFREE_GUEST_DEFERRED_CTORS_MAX;
    }
    bfree_guest_serial_lit("[desktop_qt] deferred ctors: begin n=");
    bfree_guest_serial_hex_u64((uint64_t)n);
    bfree_guest_serial_lit("\n");

    mmap_stack = bfree_guest_mmap_ctor_stack_at(BFREE_GUEST_CTOR_MMAP_STACK_BYTES, &ctor_top);
    if (!mmap_stack || ctor_top == 0) {
        bfree_guest_serial_lit("[desktop_qt] deferred ctors: mmap stack failed\n");
        return;
    }
    saved_musl = bfree_guest_musl_malloc_ready;
    saved_ctor_bump = bfree_guest_ctor_bump_mode;
    saved_ctor_hybrid = bfree_guest_ctor_bump_hybrid;
    bfree_guest_ctor_bump_mode = 1;
    bfree_guest_ctor_bump_hybrid = 1;
    bfree_guest_ctor_bump_off = 0;
    if (bfree_guest_mmap_ctor_bump_arena() != 0) {
        bfree_guest_serial_lit("[desktop_qt] deferred ctors: bump mmap fail (musl+fallback)\n");
        bfree_guest_ctor_bump_base = 0;
        bfree_guest_ctor_bump_cap = 0;
        (void)bfree_guest_ensure_fallback_heap();
    }
    __asm__ volatile("mov %%rsp, %0" : "=r"(saved_rsp));
    for (i = 0; i < n; ++i) {
        void (*fn)(void) = g_deferred_ctors[i];
        unsigned idx = g_deferred_ctor_idx[i];
        if (!fn || !bfree_guest_ctor_ptr_looks_valid(fn))
            continue;
        if (bfree_guest_should_skip_deferred_ctor(fn, idx)) {
            if (bfree_guest_is_qrc_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qrc -> main)\n");
            else if (idx == 8u)
                bfree_guest_serial_lit("[desktop_qt] deferred ctor[8] skipped (QWSI)\n");
            else if (bfree_guest_is_qqmldebug_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (QML debug)\n");
            else if (bfree_guest_is_qthread_unix_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qthread_unix)\n");
            else if (bfree_guest_is_qlibrary_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qlibrary)\n");
            else if (bfree_guest_is_eh_globals_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (eh_globals)\n");
            else if (idx == 0x11u || idx == 0x14u)
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (QML debug idx)\n");
            else if (bfree_guest_is_qhttpthreaddelegate_ctor(fn))
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qhttpthreaddelegate)\n");
            else if (idx == 0x1au)
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qthread_unix idx)\n");
            else if (idx == 0x1bu)
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (qlibrary idx)\n");
            else if (idx == 0x1cu)
                bfree_guest_serial_lit("[desktop_qt] deferred ctor[0x1c] skipped (eh_globals)\n");
            else
                bfree_guest_serial_lit("[desktop_qt] deferred ctor skipped (libstdc++ late)\n");
            continue;
        }
        bfree_guest_serial_lit("[desktop_qt] deferred ctor run idx=");
        bfree_guest_serial_hex_u64((uint64_t)idx);
        bfree_guest_serial_lit(" @ ");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)fn);
        bfree_guest_serial_lit("\n");
        g_ctor_stack_ctx.session_fn = fn;
        bfree_guest_fill_auxv_tables();
        bfree_guest_switch_rsp(ctor_top);
        bfree_guest_call_saved_session_fn();
        bfree_guest_switch_rsp(saved_rsp);
        bfree_guest_serial_lit("[desktop_qt] deferred ctor done\n");
    }
    bfree_guest_ctor_bump_mode = saved_ctor_bump;
    bfree_guest_ctor_bump_hybrid = saved_ctor_hybrid;
    bfree_guest_musl_malloc_ready = saved_musl;
    /* Deferred phase uses hybrid bump on mmap stack; restore exec-stack musl-only malloc. */
    bfree_guest_ctor_bump_mode = 0;
    bfree_guest_ctor_bump_hybrid = 0;
    bfree_guest_ctor_bump_off = 0;
    bfree_guest_on_mmap_ctor_stack = 0;
    bfree_guest_musl_malloc_ready = 1;
    bfree_guest_enable_musl_malloc();
    bfree_guest_serial_lit("[desktop_qt] deferred ctors: end\n");
}

#include "../userland/libc/musl_libc_libc.h"

static void bfree_guest_force_single_thread_libc(void)
{
    /* need_locks=-1 runs pthread lock setup on first writev -> abort(); guest uses noop __lock. */
    __libc.can_do_threads = 1;
    __libc.threaded = 0;
    __libc.need_locks = 0;
}

/* libc lock.o removed at link — single-threaded guest must not futex-spin in __lock/__lockfile. */
extern "C" void __lock(volatile int *l)
{
    (void)l;
}

extern "C" void __unlock(volatile int *l)
{
    if (l && *l)
        *l = 0;
}

extern "C" int __lockfile(FILE *f)
{
    (void)f;
    return 0;
}

extern "C" void __unlockfile(FILE *f)
{
    (void)f;
}

extern "C" void __ofl_lock(void)
{
}

extern "C" void __ofl_unlock(void)
{
}

#define BFREE_LINUX_SYS_ARCH_PRCTL 158L
#define BFREE_ARCH_SET_FS          0x1002L
#define BFREE_LINUX_SYS_FUTEX      202L
#define BFREE_LINUX_SYS_CLOCK_NANOSLEEP 230L
#define BFREE_FUTEX_WAIT           0
#define BFREE_FUTEX_WAKE           1
#define BFREE_FUTEX_PRIVATE_FLAG   128

static unsigned g_guest_syscall_trace;
static unsigned g_guest_qv4_futex_trace;

static int bfree_guest_futex_op_is_wait(int op)
{
    const int base = op & ~BFREE_FUTEX_PRIVATE_FLAG;
    return base == BFREE_FUTEX_WAIT || base == 9 || base == 11 || base == 13;
}

static long bfree_guest_qv4_futex(long uaddr, long op, long val, long timeout, long uaddr2, long val3)
{
    if (g_guest_qv4_futex_trace < 64u) {
        ++g_guest_qv4_futex_trace;
        bfree_guest_serial_lit("[qv4] futex op=");
        bfree_guest_serial_hex_u64((uint64_t)(long)op);
        bfree_guest_serial_lit(" uaddr=");
        bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)uaddr);
        bfree_guest_serial_lit(" val=");
        bfree_guest_serial_hex_u64((uint64_t)(long)val);
        bfree_guest_serial_lit("\n");
    }
    if (bfree_guest_futex_op_is_wait((int)op)) {
        bfree_pthread_pump_qv4();
        bfree_guest_errno_storage = EAGAIN;
        return -1L;
    }
    if ((op & ~BFREE_FUTEX_PRIVATE_FLAG) == BFREE_FUTEX_WAKE)
        return 1L;
    return 0L;
}

static long bfree_guest_qv4_clock_nanosleep(long clockid, long flags, long request, long remain)
{
    static unsigned g_qv4_clock_nanosleep_diag;
    (void)clockid;
    (void)flags;
    if (g_qv4_clock_nanosleep_diag < 32u) {
        ++g_qv4_clock_nanosleep_diag;
        bfree_guest_qv4_trace_tag("clock_nanosleep");
    }
    bfree_pthread_pump_qv4();
    if (remain)
        *(struct timespec *)(uintptr_t)remain = {0, 0};
    return 0L;
}

extern "C" long syscall(long number, ...)
{
    unsigned long a0, a1, a2, a3, a4, a5;
    va_list ap;
    long ret;

    va_start(ap, number);
    a0 = va_arg(ap, unsigned long);
    a1 = va_arg(ap, unsigned long);
    a2 = va_arg(ap, unsigned long);
    a3 = va_arg(ap, unsigned long);
    a4 = va_arg(ap, unsigned long);
    a5 = va_arg(ap, unsigned long);
    va_end(ap);

    if (bfree_guest_qv4_mmap_active) {
        if (number == BFREE_LINUX_SYS_FUTEX)
            return bfree_guest_qv4_futex((long)a0, (long)a1, (long)a2, (long)a3, (long)a4, (long)a5);
        if (number == BFREE_LINUX_SYS_CLOCK_NANOSLEEP)
            return bfree_guest_qv4_clock_nanosleep((long)a0, (long)a1, (long)a2, (long)a3);
    }

    if (g_guest_syscall_trace < 16u && (number == 202L || number == 232L || number == 7L)) {
        ++g_guest_syscall_trace;
        bfree_guest_serial_lit("[guest-syscall]\n");
    }

    register long r10 __asm__("r10") = (long)a3;
    register long r8 __asm__("r8") = (long)a4;
    register long r9 __asm__("r9") = (long)a5;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(number), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static long bfree_guest_syscall2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return ret;
}

/*
 * musl __errno_location reads %fs:0. __init_libc / __mmap can run before __init_tls
 * sets FS → #PF CR2=0. Bootstrap a minimal TCB on a static buffer first.
 */
static char bfree_guest_early_tcb[256] __attribute__((aligned(64)));

/* musl errno = *(int *)(%fs:0 + 0x34). Override libc so __mmap works before TLS. */
int bfree_guest_errno_storage;

extern "C" int *__errno_location(void)
{
    return &bfree_guest_errno_storage;
}

extern "C" int *___errno_location(void) __attribute__((alias("__errno_location")));

/*
 * musl __init_tls reads auxv[AT_PHDR] and auxv[AT_PHNUM] (sparse array indexed by
 * AT_*), NOT (type,value) pairs. Passing pair layout made AT_PHDR = sizeof(Phdr)
 * (~56) → walk from VA 0x38 → #PF CR2=0 in __malloc_alloc_meta.
 *
 * Kernel PT_LOAD starts at file offset 0x1000 / VA 0x2800000, so the on-disk PHDR
 * table at file offset 0x40 is not mapped — embed program headers in .rodata.
 * Regenerate after desktop.elf link: python3 tools/print_desktop_phdrs.py
 */
/* Regenerate after desktop.elf link: python3 tools/print_desktop_phdrs.py */
static const Elf64_Phdr bfree_guest_phdrs[] = {
    { PT_LOAD, PF_R | PF_W | PF_X, 0x1000, 0x2800000, 0x2800000, 0x1db3a40, 0x1db3a40, 0x1000 },
    { PT_TLS, PF_R, 0x1198c30, 0x3997c30, 0x3997c30, 0x28, 0xa8, 0x10 },
    { PT_GNU_EH_FRAME, PF_R, 0x0, 0x0, 0x0, 0x0, 0x0, 0x10 },
};

static unsigned char bfree_guest_at_random[16];

/* __init_tls: sparse auxv[AT_*].  malloc/getauxval: (type,value)* pairs via __libc.auxv. */
static size_t bfree_guest_auxv_sparse[40];
static size_t bfree_guest_auxv_pairs[16];

static void bfree_guest_fill_auxv_tables(void)
{
    const size_t phnum = sizeof(bfree_guest_phdrs) / sizeof(bfree_guest_phdrs[0]);

    memset(bfree_guest_auxv_sparse, 0, sizeof(bfree_guest_auxv_sparse));
    bfree_guest_auxv_sparse[AT_PAGESZ] = 4096;
    bfree_guest_auxv_sparse[AT_PHENT] = sizeof(Elf64_Phdr);
    bfree_guest_auxv_sparse[AT_PHNUM] = phnum;
    bfree_guest_auxv_sparse[AT_PHDR] = (size_t)(uintptr_t)bfree_guest_phdrs;
    bfree_guest_auxv_sparse[AT_ENTRY] = 0x2800000;
    bfree_guest_auxv_sparse[AT_RANDOM] = (size_t)(uintptr_t)bfree_guest_at_random;

    bfree_guest_auxv_pairs[0] = AT_PAGESZ;
    bfree_guest_auxv_pairs[1] = 4096;
    bfree_guest_auxv_pairs[2] = AT_PHENT;
    bfree_guest_auxv_pairs[3] = sizeof(Elf64_Phdr);
    bfree_guest_auxv_pairs[4] = AT_PHNUM;
    bfree_guest_auxv_pairs[5] = phnum;
    bfree_guest_auxv_pairs[6] = AT_PHDR;
    bfree_guest_auxv_pairs[7] = (size_t)(uintptr_t)bfree_guest_phdrs;
    bfree_guest_auxv_pairs[8] = AT_ENTRY;
    bfree_guest_auxv_pairs[9] = 0x2800000;
    bfree_guest_auxv_pairs[10] = AT_RANDOM;
    bfree_guest_auxv_pairs[11] = (size_t)(uintptr_t)bfree_guest_at_random;
    bfree_guest_auxv_pairs[12] = 0;
    bfree_guest_auxv_pairs[13] = 0;

    __libc.page_size = 4096;
    __libc.auxv = bfree_guest_auxv_pairs;
    bfree_guest_install_static_env();
}

static void bfree_guest_seed_at_random(void)
{
    size_t i;
    for (i = 0; i < sizeof(bfree_guest_at_random); ++i)
        bfree_guest_at_random[i] = (unsigned char)(0x5aU + (unsigned char)i);
}

static void bfree_guest_serial_hex(uintptr_t v)
{
    char buf[24];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; ++i) {
        unsigned nib = (unsigned)((v >> (60 - i * 4)) & 0xf);
        buf[2 + i] = (char)(nib < 10 ? '0' + (int)nib : 'a' + (int)nib - 10);
    }
    buf[18] = '\0';
    bfree_guest_serial_lit(buf);
}

static void bfree_guest_serial_step(char step)
{
    char buf[4];
    buf[0] = '[';
    buf[1] = step;
    buf[2] = ']';
    buf[3] = '\0';
    bfree_guest_serial(buf);
}

extern "C" void bfree_guest_serial_step_c(char step)
{
    bfree_guest_serial_step(step);
}

extern "C" void bfree_guest_serial_hex_u64(uint64_t v)
{
    bfree_guest_serial_hex((uintptr_t)v);
}

static uintptr_t bfree_guest_read_fs0(void)
{
    uintptr_t tp = 0;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(tp));
    return tp;
}

static int bfree_guest_exec_rsp_valid(uintptr_t rsp)
{
    if (rsp < BFREE_GUEST_EXEC_STACK_FLOOR)
        return 0;
    /* pre-mmap exec stack saved before RSP switch (e.g. 0x13fff80) */
    if (rsp < (uintptr_t)BFREE_GUEST_CTOR_MMAP_VA)
        return 1;
    return rsp < (uintptr_t)BFREE_GUEST_CTOR_MMAP_VA + BFREE_GUEST_CTOR_MMAP_STACK_BYTES;
}

static int bfree_guest_qt_on_exec_rsp(uintptr_t exec_rsp)
{
    return exec_rsp != 0 && bfree_guest_exec_rsp_valid(exec_rsp)
        && (bfree_guest_on_mmap_ctor_stack || bfree_guest_qv4_mmap_active);
}

static void bfree_guest_sync_stack_canary(void)
{
    uintptr_t guard = 0;

    __asm__ volatile("mov %%fs:0x28, %0" : "=r"(guard));
    if (guard != 0)
        return;
    __init_ssp(0);
}

static void bfree_guest_init_musl_tls(void)
{
    long prctl_ret;

    bfree_guest_serial_step('A');
    memset(&__libc, 0, sizeof(__libc));
    __libc.can_do_threads = 1;
    __libc.need_locks = 0;
    __libc.page_size = 4096;
    memset(bfree_guest_early_tcb, 0, sizeof(bfree_guest_early_tcb));
    *(uintptr_t *)bfree_guest_early_tcb = (uintptr_t)bfree_guest_early_tcb;
    prctl_ret = bfree_guest_syscall2(BFREE_LINUX_SYS_ARCH_PRCTL, BFREE_ARCH_SET_FS,
                                     (long)(uintptr_t)bfree_guest_early_tcb);
    bfree_guest_serial_step('B');
    bfree_guest_serial_hex((uintptr_t)prctl_ret);
    bfree_guest_serial_hex(bfree_guest_read_fs0());

    bfree_guest_seed_at_random();
    bfree_guest_fill_auxv_tables();
    memset(__malloc_context, 0, BFREE_MUSL_MALLOC_CONTEXT_BYTES);

    bfree_guest_serial_step('C');
    bfree_guest_serial_hex((uintptr_t)bfree_guest_phdrs);
    bfree_guest_serial_hex((uintptr_t)__libc.auxv);

    __init_tls(bfree_guest_auxv_sparse);
    bfree_guest_sync_stack_canary();
    bfree_guest_force_single_thread_libc();
    bfree_guest_fill_auxv_tables();

    bfree_guest_serial_step('D');
    bfree_guest_serial_hex(bfree_guest_read_fs0());
    bfree_guest_serial_hex((uintptr_t)__libc.auxv);
    bfree_guest_fill_auxv_tables();
    bfree_guest_enable_musl_malloc();
    bfree_guest_serial_step_c('P');
}

extern "C" void bfree_guest_rebind_musl_fs(void)
{
    bfree_guest_syscall2(BFREE_LINUX_SYS_ARCH_PRCTL, BFREE_ARCH_SET_FS,
                         (long)(uintptr_t)bfree_guest_early_tcb);
}

extern "C" void bfree_guest_refresh_libc_auxv(void)
{
    bfree_guest_fill_auxv_tables();
    bfree_guest_force_single_thread_libc();
    bfree_guest_sync_stack_canary();
}

extern "C" void __stack_chk_fail(void)
{
    void *ra0 = __builtin_return_address(0);

    bfree_guest_serial_lit("[desktop_qt] stack_chk_fail ra0=");
    bfree_guest_serial_hex_u64((uint64_t)(uintptr_t)ra0);
    bfree_guest_serial_lit("\n");
    for (;;)
        __asm__ volatile("hlt");
}

extern "C" void bfree_guest_post_tls_banners(void)
{
    bfree_guest_serial_step('E');
    bfree_guest_serial_lit("[desktop_qt] init_array: skipped (BFREE_SKIP_GUEST_INIT_ARRAY)\n");
    bfree_guest_serial_step('F');
    bfree_guest_serial_lit("[desktop_qt] crt0: init_array begin\n");
    bfree_guest_serial_lit("[desktop_qt] crt0: calling main\n");
}

/* Called from crt0.S before main — logs each ctor for serial bring-up.
 * Set BFREE_SKIP_GUEST_INIT_ARRAY=1 at compile time to reach main without static ctors. */
extern "C" void bfree_guest_run_init_array(void)
{
    bfree_guest_init_musl_tls();
#if defined(BFREE_SKIP_GUEST_INIT_ARRAY)
    bfree_guest_post_tls_banners();
#else
    typedef void (*ctor_t)(void);
    extern ctor_t __init_array_start[];
    extern ctor_t __init_array_end[];
    ctor_t *p;
    /* Execute at most the first N non-null ctors.
     * This is for debugging: Qt static init can crash deep into init_array,
     * so we binary-search a small working prefix. */
#ifndef BFREE_GUEST_INIT_ARRAY_MAX
#define BFREE_GUEST_INIT_ARRAY_MAX 0xFFFFFFFFu
#endif
    unsigned n = 0;
    unsigned executed = 0;

    bfree_guest_serial("[desktop_qt] init_array: C runner begin\n");
    bfree_guest_serial("[desktop_qt] init_array ptr0=");
    bfree_guest_serial_hex((uintptr_t)*__init_array_start);
    bfree_guest_serial("\n");
    {
        uintptr_t ctor_top = 0;
        void *ctor_stack = 0;
        int saved_musl = bfree_guest_musl_malloc_ready;
        int saved_ctor_bump = bfree_guest_ctor_bump_mode;
        int saved_ctor_hybrid = bfree_guest_ctor_bump_hybrid;
        size_t saved_ctor_bump_off = bfree_guest_ctor_bump_off;

        ctor_stack = bfree_guest_alloc_ctor_stack(&ctor_top);
        if (!ctor_stack || ctor_top == 0) {
            bfree_guest_serial("[desktop_qt] init_array: bss ctor stack failed\n");
        } else {
            bfree_guest_ctor_bump_mode = 1;
            bfree_guest_ctor_bump_hybrid = 0;
            bfree_guest_musl_malloc_ready = 0;
            bfree_guest_ctor_bump_base = bfree_guest_bump_heap;
            bfree_guest_ctor_bump_cap = sizeof(bfree_guest_bump_heap);
            bfree_guest_ctor_bump_off = 0;
            __asm__ volatile("mov %%rsp, %0" : "=r"(g_ctor_stack_ctx.saved_rsp));
            bfree_guest_serial_lit("[desktop_qt] init_array: bss stack + bump (no mmap)\n");
            for (p = __init_array_start; p < __init_array_end; ++p, ++n) {
                if (!*p)
                    continue;
                if (executed >= (unsigned)BFREE_GUEST_INIT_ARRAY_MAX) {
                    bfree_guest_serial("[desktop_qt] init_array: max reached\n");
                    break;
                }
                bfree_guest_serial("[desktop_qt] ctor[");
                bfree_guest_serial_hex((uintptr_t)n);
                bfree_guest_serial("] @ ");
                bfree_guest_serial_hex((uintptr_t)*p);
                bfree_guest_serial("\n");
                if (bfree_guest_skip_init_array_ctor((uintptr_t)*p)) {
                    bfree_guest_serial("[desktop_qt] ctor skipped (bad init_array)\n");
                    ++executed;
                    continue;
                }
                if (bfree_guest_defer_qrc_ctor((uintptr_t)*p)) {
                    bfree_guest_serial("[desktop_qt] ctor deferred (qrc -> main)\n");
                    ++executed;
                    continue;
                }
                if (bfree_guest_defer_late_ctor((uintptr_t)*p, n)) {
                    bfree_guest_record_deferred_ctor(*p, n);
                    bfree_guest_serial("[desktop_qt] ctor deferred (late -> main)\n");
                    ++executed;
                    continue;
                }
                bfree_guest_fill_auxv_tables();
                bfree_guest_switch_rsp(ctor_top);
                (*p)();
                bfree_guest_switch_rsp(g_ctor_stack_ctx.saved_rsp);
                bfree_guest_serial("[desktop_qt] ctor done\n");
                ++executed;
            }
            bfree_guest_ctor_bump_mode = saved_ctor_bump;
            bfree_guest_ctor_bump_hybrid = saved_ctor_hybrid;
            bfree_guest_musl_malloc_ready = saved_musl;
            bfree_guest_ctor_bump_off = saved_ctor_bump_off;
        }
    }
    bfree_guest_serial("[desktop_qt] init_array: C runner end\n");
#endif
}

static unsigned g_qreg_sanitize_diag;

/* Q_GLOBAL_STATIC guard for resourceGlobalData (holder-8). If it reads 0 while the list
 * already has entries, qRegisterResourceData re-inits and zeroes the live QList. */
static void bfree_guest_resource_pin_global_guard(void)
{
    const uintptr_t holder = (uintptr_t)BFREE_GUEST_QT_RESOURCE_HOLDER_VA;
    const int64_t n = *(int64_t *)(holder + 0x28);
    volatile unsigned char *const zgv =
        (volatile unsigned char *)(holder - 8u);
    volatile unsigned char *const hguard =
        (volatile unsigned char *)(holder - 0x0fu);

    if (n <= 0)
        return;
    if (*zgv == 0 || *zgv == 1)
        *zgv = 2;
    if (*hguard != 0xff)
        *hguard = 0xff;
}

static void bfree_guest_resource_list_sanitize(void)
{
    char *const h = (char *)(uintptr_t)BFREE_GUEST_QT_RESOURCE_HOLDER_VA;
    void **ptrs;
    int64_t n;
    int64_t w = 0;
    int64_t i;

    if (!h)
        return;
    bfree_guest_resource_pin_global_guard();
    ptrs = *(void ***)(h + 0x20);
    n = *(int64_t *)(h + 0x28);
    if (n <= 0)
        return;
    if (!ptrs) {
        *(int64_t *)(h + 0x28) = 0;
        return;
    }
    for (i = 0; i < n; ++i) {
        void *r = ptrs[i];
        if (!r)
            continue;
        /* Drop resource roots whose vtable is bogus (uninitialized / deferred
         * ctor never ran => vtable pointer == 1). Such a root crashes
         * QResourceRoot::findNode at `call *0x10(vtable)` (CR2=0x11). The
         * object memory is allocated (r != NULL) so reading the vtable qword
         * is safe; a valid guest vtable lives well above 0x100000. */
        if (*(uintptr_t *)r < (uintptr_t)0x100000)
            continue;
        ptrs[w++] = r;
    }
    if (w == n)
        return;
    *(int64_t *)(h + 0x28) = w;
    if (g_qreg_sanitize_diag < 8u) {
        ++g_qreg_sanitize_diag;
        bfree_guest_serial_lit("[desktop_qt] qresource sanitize n=");
        bfree_guest_serial_hex_u64((uint64_t)n);
        bfree_guest_serial_lit(" -> ");
        bfree_guest_serial_hex_u64((uint64_t)w);
        bfree_guest_serial_lit("\n");
    }
}

extern "C" void bfree_guest_qresource_sanitize(void)
{
    bfree_guest_resource_list_sanitize();
}

/* Gate 1: stock isThisThread() until the event loop. Never true during STAGE 5 qrc. */
static int g_bfree_typeloader_main_ok;

extern "C" void bfree_guest_set_typeloader_main_ok(int on)
{
    g_bfree_typeloader_main_ok = on ? 1 : 0;
    bfree_guest_serial_lit("[desktop_qt] typeloader main ok=");
    bfree_guest_serial_lit(on ? "1\n" : "0\n");
}

extern "C" int bfree_guest_typeloader_main_ok(void)
{
    return g_bfree_typeloader_main_ok;
}

extern "C" bool __real__Z21qRegisterResourceDataiPKhS0_S0_(int version, const unsigned char *tree,
                                                           const unsigned char *name,
                                                           const unsigned char *data);

extern "C" bool __wrap__Z21qRegisterResourceDataiPKhS0_S0_(int version, const unsigned char *tree,
                                                             const unsigned char *name,
                                                             const unsigned char *data)
{
    bfree_guest_resource_list_sanitize();
    const bool ok =
        __real__Z21qRegisterResourceDataiPKhS0_S0_(version, tree, name, data);
    bfree_guest_resource_list_sanitize();
    return ok;
}

#include "../userland/desktop_qt/guest_mvp_shell_qml.inc"

extern "C" const char *bfree_guest_mvp_shell_qml_data(void)
{
    return reinterpret_cast<const char *>(kGuestMvpShellQml);
}

extern "C" int bfree_guest_mvp_shell_qml_size(void)
{
    return (int)sizeof(kGuestMvpShellQml);
}
