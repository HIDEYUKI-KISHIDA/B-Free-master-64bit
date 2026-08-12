// bfree_epoll.h - B-Free用の最小epoll互換宣言

#ifndef BFREE_EPOLL_H
#define BFREE_EPOLL_H

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#else
#ifndef __BFREE_EPOLL_TYPES_DEFINED
#define __BFREE_EPOLL_TYPES_DEFINED
typedef long time_t;
typedef struct timespec {
    time_t tv_sec;
    long   tv_nsec;
} timespec;
typedef struct timeval {
    time_t tv_sec;
    long   tv_usec;
} timeval;
typedef struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
} itimerspec;
struct pollfd {
    int fd;
    short events;
    short revents;
};
typedef unsigned long nfds_t;
typedef struct {
    unsigned long fds_bits[16];
} fd_set;
#endif // __BFREE_EPOLL_TYPES_DEFINED
#endif




#include <stdint.h>

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct pollfd;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};

#define EPOLL_CLOEXEC 0x80000

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN 0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDNORM 0x040
#define EPOLLRDBAND 0x080
#define EPOLLWRNORM 0x100
#define EPOLLWRBAND 0x200
#define EPOLLMSG 0x400
#define EPOLLRDHUP 0x2000
#define EPOLLEXCLUSIVE (1u << 28)
#define EPOLLWAKEUP (1u << 29)
#define EPOLLONESHOT (1u << 30)
#define EPOLLET (1u << 31)

#define BFREE_FB0_FD 0x2000
/* Ring3 fixed VA for sys_mmap(BFREE_FB0_FD); must be < VMM_USER_VA_BYTES (vmm.h). */
#define BFREE_FB0_USER_MMAP_BASE     0x01400000u
/* Anonymous mmap/brk heap for musl/Qt; must stay within VMM_USER_VA_BYTES. */
#define BFREE_GUEST_HEAP_BASE        0x03C00000u
/* Up to guest heap base: stack top is also 0x01400000, FB maps upward. */
#define BFREE_FB0_USER_MMAP_MAX_SIZE (BFREE_GUEST_HEAP_BASE - BFREE_FB0_USER_MMAP_BASE)
/* 0x08000000-0x18000000: ctor mmap stack (256MiB musl / 224MiB hybrid);
 * 0x16000000-0x18000000: hybrid ctor bump (32MiB); QV4 @0x18000000; fallback @0x19000000;
 * 0x21000000-0x29000000: QV4 MemoryManager 128MiB slab (QQmlEngine). */
#define BFREE_GUEST_HEAP_LIMIT       0x2A000000u
#define BFREE_INPUT_EVENT_FD 0x3000
#define BFREE_TIMERFD_FD_BASE 0x3200
#define BFREE_MAX_TIMERFD 16
/* Qt QEventDispatcherUNIX wakeup pipe + epoll (Linux guest syscalls in kernel). */
#define BFREE_GUEST_PIPE_RD_FD   0x3400
#define BFREE_GUEST_PIPE_WR_FD   0x3401
#define BFREE_GUEST_EPOLL_FD_BASE 0x3500
#define BFREE_GUEST_EVENTFD_BASE  0x3600
#define BFREE_MAX_GUEST_EPOLL    8
#define BFREE_MAX_GUEST_EVENTFD  32
#define BFREE_SIGNALFD_FD_BASE   0x3680
#define BFREE_MAX_SIGNALFD       8
#define BFREE_MAX_GUEST_EPOLL_WATCHES 32

#define BFREE_KEY_BASE 0x1100
#define BFREE_KEY_UP (BFREE_KEY_BASE + 1)
#define BFREE_KEY_DOWN (BFREE_KEY_BASE + 2)
#define BFREE_KEY_RIGHT (BFREE_KEY_BASE + 3)
#define BFREE_KEY_LEFT (BFREE_KEY_BASE + 4)
#define BFREE_KEY_HOME (BFREE_KEY_BASE + 5)
#define BFREE_KEY_END (BFREE_KEY_BASE + 6)
#define BFREE_KEY_DELETE (BFREE_KEY_BASE + 7)
#define BFREE_KEY_PAGEUP (BFREE_KEY_BASE + 8)
#define BFREE_KEY_PAGEDOWN (BFREE_KEY_BASE + 9)
#define BFREE_KEY_INSERT (BFREE_KEY_BASE + 10)
#define BFREE_KEY_F1 (BFREE_KEY_BASE + 11)
#define BFREE_KEY_F2 (BFREE_KEY_BASE + 12)
#define BFREE_KEY_F3 (BFREE_KEY_BASE + 13)
#define BFREE_KEY_F4 (BFREE_KEY_BASE + 14)


#define BFREE_SYSCALL_POLL_INPUT_EVENT 0
#define BFREE_SYSCALL_GET_FRAMEBUFFER_INFO 1
#define BFREE_SYSCALL_CLEAR_SCREEN 2
#define BFREE_SYSCALL_GET_TIME 3
#define BFREE_SYSCALL_INPUT_EVENT_PENDING 4
#define BFREE_SYSCALL_TIMERFD_CREATE 5
#define BFREE_SYSCALL_TIMERFD_SETTIME 6
#define BFREE_SYSCALL_TIMERFD_GETTIME 7
#define BFREE_SYSCALL_TIMERFD_READ 8
#define BFREE_SYSCALL_TIMERFD_PENDING 9
#define BFREE_SYSCALL_TIMERFD_CLOSE 10
#define BFREE_SYSCALL_SIGNAL_SETMASK 11
#define BFREE_SYSCALL_SIGNAL_PENDING 12
#define BFREE_SYSCALL_SIGNAL_POST 13
#define BFREE_SYSCALL_SIGNAL_HAS_READY 14
#define BFREE_SYSCALL_SIGNAL_CONSUME 15

// --- fbdev/ioctl用SYSCALL番号 ---
#define BFREE_SYSCALL_FBDEV_IOCTL 20
#define BFREE_SYSCALL_INPUT_IOCTL 21
#define BFREE_SYSCALL_IOCTL 22
#define BFREE_SYSCALL_GET_TK2_SNAPSHOT 23
#define BFREE_SYSCALL_DEBUG_SERIAL_WRITE 24

#define BFREE_SIGNAL_WORDS 2

typedef struct {
    uint32_t abi_version;
    uint32_t flags;
    uint64_t tick_us;
    uint32_t task_count;
    uint32_t semaphore_count;
    uint32_t eventflag_count;
    uint32_t mailbox_count;
    uint32_t device_count;
    char kernel_version[64];
    char build_date[32];
    char cpu_state[16];
} bfree_tk2_snapshot_t;

#define TFD_TIMER_ABSTIME 1
#define TFD_CLOEXEC 0x80000
#define TFD_NONBLOCK 0x800

int bfree_epoll_create1(int flags);
int bfree_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int bfree_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int bfree_epoll_is_emulated_fd(int fd);
int bfree_epoll_close_emulated(int fd);
int bfree_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int bfree_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *curr_value);
int bfree_timerfd_create(int clockid, int flags);
int bfree_timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
int bfree_timerfd_gettime(int fd, struct itimerspec *curr_value);
int bfree_get_tk2_snapshot(bfree_tk2_snapshot_t *out, uint32_t out_size);
int bfree_export_tk2_snapshot_json(const char *path);

#endif // BFREE_EPOLL_H