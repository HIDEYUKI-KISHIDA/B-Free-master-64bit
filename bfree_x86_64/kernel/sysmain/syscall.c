// syscall.c - SYSCALL/SYSRET経路Cハンドラ（仕様書v2.4準拠）
//
// * System V ABI順（RDI, RSI, RDX, RCX, R8, R9）で受け取る
// * syscall番号で個別APIをdispatch
#include <stdint.h>
#include <stddef.h>
#include "../string.h"

#include "../fbdev.h"
#include "../gpu_backend.h"
#include "../include/tk/kernel.h"
#include "timer_manager.h"
#include "security_policy.h"
#include "bfree_guest_thread.h"
#include "elf_loader.h"
#include "process.h"
#include "../../userland/libc/bfree_epoll.h"

extern int vmm_map_page(page_table_t *pt, uint64_t vaddr, uint64_t paddr, uint64_t flags);
extern int vmm_unmap_page(page_table_t *pt, uint64_t vaddr);
extern void vmm_drop_identity_alias(page_table_t *pt, uint64_t phys);
extern int vmm_user_page_mapped(page_table_t *pt, uint64_t vaddr);
extern int vmm_user_virt_to_phys(page_table_t *pt, uint64_t vaddr, uint64_t *paddr_out);

extern page_table_t kernel_page_table;

#ifndef BFREE_GUEST_FD_TABLE_SIZE
#define BFREE_GUEST_FD_TABLE_SIZE 64
#endif
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];

extern void bfree_enable_user_fpu(void);

extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern void uart_puthex64(uint64_t val);
extern void *pmm_alloc(void);

#define MAP_SHARED    0x01L
#define MAP_PRIVATE   0x02L
#define MAP_ANONYMOUS 0x20L
#define MAP_FIXED     0x10L

static uint64_t g_guest_heap_next = (uint64_t)BFREE_GUEST_HEAP_BASE;
static uint64_t g_guest_brk = (uint64_t)BFREE_GUEST_HEAP_BASE;

void bfree_guest_heap_reset(void)
{
    g_guest_heap_next = (uint64_t)BFREE_GUEST_HEAP_BASE;
    g_guest_brk = (uint64_t)BFREE_GUEST_HEAP_BASE;
}

/* init.elf PT_LOAD at 0x400000 (3 pages in serial). Drop User mappings after
 * exec_initrd so later mmap/brk cannot alias stale init pages. IMPORTANT:
 * kernel_page_table BSS spans past 0x400000, so leaving PTE=0 here makes
 * vmm_clone_kernel_page_table #PF when it memcpy's that object. Restore
 * identity supervisor mappings (Present|RW) after clearing User PT_LOAD. */
#define BFREE_INIT_USER_LOAD_BASE  0x00400000ULL
#define BFREE_INIT_USER_LOAD_SIZE  0x00010000ULL

static void bfree_exec_unmap_init_legacy(page_table_t *pt)
{
    uint64_t p;

    if (!pt) {
        return;
    }
    for (p = BFREE_INIT_USER_LOAD_BASE;
         p < BFREE_INIT_USER_LOAD_BASE + BFREE_INIT_USER_LOAD_SIZE;
         p += PAGE_SIZE) {
        (void)vmm_unmap_page(pt, p);
        /* Identity map: VA == PA, ring0 only (no User bit). */
        (void)vmm_map_page(pt, p, p, 0x003ULL);
    }
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
    uart_puts("[EXEC] restored identity over init [");
    uart_puthex64(BFREE_INIT_USER_LOAD_BASE);
    uart_puts(", ");
    uart_puthex64(BFREE_INIT_USER_LOAD_BASE + BFREE_INIT_USER_LOAD_SIZE);
    uart_puts(")\n");
}

/* syscall_entry.S: if knl_syscall_handler returns this, replace user RCX/R11/RSP for SYSRET */
#define BFREE_SYSRET_EXEC_TRANSFER ((long)-4094)
#define BFREE_SYSRET_FORK_PARENT   ((long)-4093)
#define BFREE_SYSRET_COOP_SWITCH   ((long)-4092)
#define BFREE_SYSRET_SIGNAL        ((long)-4091)

uint64_t g_bfree_sysret_exec_rsp;
uint64_t g_bfree_sysret_exec_rcx;
uint64_t g_bfree_sysret_exec_r11;
uint64_t g_bfree_sysret_exec_rdi;
uint64_t g_bfree_sysret_exec_rsi;
uint64_t g_bfree_sysret_exec_rdx;
/* Non-zero: syscall_entry.S loads this into CR3 before the new user RSP (vfork child AS). */
uint64_t g_bfree_sysret_exec_cr3;
/* Exec-only user RIP for BFREE_SYSRET_EXEC_TRANSFER (coop must not clobber). */
uint64_t g_bfree_exec_transfer_rip;
/* After EXEC_TRANSFER, log the first few user syscalls (desktop→busybox hang). */
static int g_bfree_post_exec_syscalls;
/* RAX loaded by BFREE_SYSRET_SIGNAL (0 at handler entry; interrupted retval on sigreturn). */
uint64_t g_bfree_sysret_sig_rax;

/* H01: callee-saved published for BFREE_SYSRET_SIGNAL (syscall_entry.S). */
uint64_t g_bfree_sig_saved_rbx;
uint64_t g_bfree_sig_saved_rbp;
uint64_t g_bfree_sig_saved_r12;
uint64_t g_bfree_sig_saved_r13;
uint64_t g_bfree_sig_saved_r14;
uint64_t g_bfree_sig_saved_r15;
uint64_t g_bfree_sig_saved_rdx;

uint64_t g_bfree_user_sysret_rcx;
uint64_t g_bfree_user_sysret_r11;
uint64_t g_bfree_user_sysret_rsp;
uint64_t g_bfree_user_sysret_rbx;
uint64_t g_bfree_user_sysret_rbp;
uint64_t g_bfree_user_sysret_r12;
uint64_t g_bfree_user_sysret_r13;
uint64_t g_bfree_user_sysret_r14;
uint64_t g_bfree_user_sysret_r15;
/* musl vfork: pop retaddr into %rdx before syscall; parent must restore it. */
uint64_t g_bfree_user_sysret_rdx;
/* Linux syscall arg6 (R9) — mmap offset / pgoff; entry asm stores before C ABI shuffle. */
uint64_t g_bfree_user_syscall_r9;
uint64_t g_bfree_fork_parent_ret;

/* Parent SYSRET context captured at fork/vfork — must not use g_bfree_user_sysret_*
 * at child exit time (those are overwritten by every subsequent child syscall).
 * Exposed to syscall_entry.S for FORK_PARENT register restore. */
uint64_t g_bfree_fork_saved_rcx;
uint64_t g_bfree_fork_saved_r11;
uint64_t g_bfree_fork_saved_rsp;
uint64_t g_bfree_fork_saved_rbx;
uint64_t g_bfree_fork_saved_rbp;
uint64_t g_bfree_fork_saved_r12;
uint64_t g_bfree_fork_saved_r13;
uint64_t g_bfree_fork_saved_r14;
uint64_t g_bfree_fork_saved_r15;
uint64_t g_bfree_fork_saved_rdx;
static uint64_t g_guest_fork_saved_fsbase;


/* Pending-connection backlog shared by AF_INET and AF_UNIX listeners. */
#ifndef BFREE_ACCEPT_Q
#define BFREE_ACCEPT_Q 4
#endif

/* H32 AF_INET (restored) */
#ifndef BFREE_LINUX_AF_INET
#define BFREE_LINUX_AF_INET 2
#endif
/* AF_INET6 rides the AF_INET slots: only ::1 and ::ffff:127.0.0.1 resolve. */
#ifndef BFREE_LINUX_AF_INET6
#define BFREE_LINUX_AF_INET6 10
#endif
#ifndef BFREE_INET_SLOTS
#define BFREE_INET_SLOTS 8
#define BFREE_INET_FD_BASE 0x3B00 /* avoid PTY 0x3A00 clash */
#define BFREE_INADDR_LOOPBACK 0x7f000001U
#define BFREE_INADDR_ANY 0U
/* F2: UDP loopback datagram queue (per receiving socket). */
#define BFREE_INET_DGRAMS      4
#define BFREE_INET_DGRAM_SIZE  512
/* QEMU slirp guest address (matches net_runtime_init). */
#define BFREE_INADDR_GUEST_LAN 0x0a00020fU /* 10.0.2.15 */

extern int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                    const uint8_t *data, size_t len);
extern void udp_register_port(uint16_t port, void (*cb)(uint32_t, uint16_t, const uint8_t *, size_t));
extern void udp_unregister_port(uint16_t port);
extern void net_runtime_poll(void);
extern int tcp_min_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
extern int tcp_min_pump(int pcb);
extern int tcp_min_send(int pcb, const uint8_t *data, size_t len);
extern int tcp_min_recv(int pcb, uint8_t *buf, size_t len);
extern void tcp_min_close(int pcb);
extern int ipv4_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, size_t len);
extern int icmp_recv(uint32_t *src_ip, void *buf, size_t max_len);
#ifndef NET_SEND_OK
#define NET_SEND_OK 0
#define NET_SEND_PENDING 1
#define NET_SEND_TIMEOUT 2
#define NET_SEND_NO_ROUTE 3
#endif
#ifndef BFREE_SOCK_RAW
#define BFREE_SOCK_RAW 3
#define BFREE_IPPROTO_ICMP 1
#endif

/* Weak stubs when ENABLE_RUNTIME_NET=0 (no e1000 / udp objects linked). */
__attribute__((weak)) int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                                   const uint8_t *data, size_t len)
{
    (void)dst_ip; (void)dst_port; (void)src_port; (void)data; (void)len;
    return -1;
}
__attribute__((weak)) void udp_register_port(uint16_t port,
    void (*cb)(uint32_t, uint16_t, const uint8_t *, size_t))
{
    (void)port; (void)cb;
}
__attribute__((weak)) void udp_unregister_port(uint16_t port)
{
    (void)port;
}
__attribute__((weak)) void net_runtime_poll(void)
{
}
__attribute__((weak)) int tcp_min_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port)
{
    (void)dst_ip; (void)dst_port; (void)src_port;
    return -101;
}
__attribute__((weak)) int tcp_min_pump(int pcb)
{
    (void)pcb;
    return -101;
}
__attribute__((weak)) int tcp_min_send(int pcb, const uint8_t *data, size_t len)
{
    (void)pcb; (void)data; (void)len;
    return -101;
}
__attribute__((weak)) int tcp_min_recv(int pcb, uint8_t *buf, size_t len)
{
    (void)pcb; (void)buf; (void)len;
    return -11;
}
__attribute__((weak)) void tcp_min_close(int pcb)
{
    (void)pcb;
}
__attribute__((weak)) int ipv4_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, size_t len)
{
    (void)dst_ip; (void)protocol; (void)payload; (void)len;
    return NET_SEND_NO_ROUTE;
}
__attribute__((weak)) int icmp_recv(uint32_t *src_ip, void *buf, size_t max_len)
{
    (void)src_ip; (void)buf; (void)max_len;
    return 0;
}

typedef struct {
    int used;
    int listening;
    int connected;
    int bound;
    int is_dgram;   /* SOCK_DGRAM: datagram queue (+ e1000 for LAN) */
    int is_v6;      /* AF_INET6 socket mapped onto the IPv4 loopback slot */
    int is_raw;     /* SOCK_RAW (BusyBox ping ICMP) */
    int ip_proto;   /* e.g. IPPROTO_ICMP=1 for raw */
    int shut_rd;
    int shut_wr;
    int nonblock;
    int so_reuseaddr;
    int so_reuseport;
    int so_keepalive;
    int so_broadcast;
    int tcp_nodelay;
    int so_linger_on;
    int so_linger_sec;
    int so_oobinline;
    int so_error;   /* pending SO_ERROR, cleared on getsockopt */
    int ip_ttl;     /* IPPROTO_IP / IP_TTL (default 64) */
    int ip_tos;     /* IPPROTO_IP / IP_TOS */
    int so_rcvbuf;
    int so_sndbuf;
    int64_t so_rcvtimeo_us;
    int64_t so_sndtimeo_us;
    uint32_t addr;
    uint16_t port;
    int accept_rd;
    int accept_wr; /* pending accepted-side write magic (-1 if none) */
    int pipe_magic;
    /* Listener backlog: pending connect() pipe pairs waiting for accept(). */
    int q_rd[BFREE_ACCEPT_Q];
    int q_wr[BFREE_ACCEPT_Q];
    int q_len;
    int listen_backlog;
    int tcp_pcb; /* >=0: minimal e1000 TCP client pcb (SOCK_STREAM LAN) */
    uint32_t peer_addr; /* dgram connect() default destination */
    uint16_t peer_port;
    int dg_head;
    int dg_count;
    uint16_t dg_len[BFREE_INET_DGRAMS];
    uint32_t dg_src_addr[BFREE_INET_DGRAMS];
    uint16_t dg_src_port[BFREE_INET_DGRAMS];
    uint8_t dg_buf[BFREE_INET_DGRAMS][BFREE_INET_DGRAM_SIZE];
} bfree_inet_sock_t;
static bfree_inet_sock_t g_inet_socks[BFREE_INET_SLOTS];
#endif

/* H02 coop dual-live resume (stubs if missing) */
#ifndef BFREE_COOP_RESUME_STUBS
#define BFREE_COOP_RESUME_STUBS 1
static int g_coop_parent_resume_mode;
static int g_coop_child_resume_mode;
static uint64_t g_coop_parent_resume_rax;
static uint64_t g_coop_child_resume_rax;
static uint64_t g_coop_parent_rcx;
static uint64_t g_coop_parent_r11;
static uint64_t g_coop_parent_rsp;
static uint64_t g_coop_parent_rbx;
static uint64_t g_coop_parent_rbp;
static uint64_t g_coop_parent_r12;
static uint64_t g_coop_parent_r13;
static uint64_t g_coop_parent_r14;
static uint64_t g_coop_parent_r15;
static uint64_t g_coop_parent_rdx;
static long g_coop_cur_nr;
static int g_guest_sys_trace;
static uint32_t g_guest_uid;
static uint32_t g_guest_euid;
static uint32_t g_guest_gid;
static uint32_t g_guest_egid;
static char g_execve_kpath_override[256];
static int g_getdents_legacy;
static int g_guest_pgid = 1;
#endif

static int g_guest_fork_active;
static int g_guest_fork_pid;
static int g_guest_fork_status;
static int g_guest_fork_status_ready;
/* 1 if this coop child was created via SYS_fork AS-copy (parent kept running).
 * Distinct from has_private_as: vfork+exec also gains a private AS, but the
 * parent was frozen and still needs the shared-AS stack snapshot restored. */
static int g_guest_fork_was_as_copy;
static int g_guest_next_pid = 2;
static uintptr_t g_guest_clear_child_tid;
static int g_guest_thread_active;
static int g_guest_thread_tid;
static int g_guest_thread_slots_used;

/* H17: registered alternate signal stack */
static void *g_sigalt_sp;
static size_t g_sigalt_size;
static int g_sigalt_disable = 1;


/* === restore compile glue (pre-wipe resume) === */
#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif
#ifndef BFREE_SIG_DFL
#define BFREE_SIG_DFL   0
#define BFREE_SIG_IGN   1
#define BFREE_SIG_CATCH 2
#define BFREE_NSIG      64
#endif
#ifndef BFREE_UNIX_SLOTS
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_UNIX_SLOTS 16
#define BFREE_UNIX_FD_BASE 0x3900
#define BFREE_UNIX_DGRAM_SIZE 256
typedef struct {
    int used;
    int listening;
    int connected;
    int is_dgram;   /* SOCK_DGRAM: single-slot datagram mailbox */
    int shut_rd;
    int shut_wr;
    int nonblock;
    int so_keepalive;
    int so_rcvbuf;
    int so_sndbuf;
    int accept_rd;
    int accept_wr; /* pending accepted-side write magic (-1 if none) */
    int pipe_magic;
    char path[96];
    char peer_path[96];  /* dgram connect() default destination */
    char dg_src[96];     /* bound path of the last datagram's sender */
    int dg_pending;
    size_t dg_len;
    unsigned char dg_buf[BFREE_UNIX_DGRAM_SIZE];
    int q_rd[BFREE_ACCEPT_Q];
    int q_wr[BFREE_ACCEPT_Q];
    int q_len;
    int listen_backlog;
} bfree_unix_sock_t;
static bfree_unix_sock_t g_unix_socks[BFREE_UNIX_SLOTS];
#endif

static int bfree_user_ptr_mapped(long ptr);
static int bfree_user_buf_mapped(uint64_t base, uint64_t len);
static int bfree_user_vaddr_mapped(uint64_t vaddr);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static uint64_t bfree_rdmsr64(uint32_t msr);
static int bfree_pty_slot_from_fd(int fd);
static int bfree_inet_from_fd(int fd);
static void bfree_inet_sock_release(int resolved);
static void bfree_pidfd_release(int resolved);
static void bfree_inotify_release(int resolved);
static void bfree_inotify_notify_vname(const char *vname, uint32_t mask);
static long bfree_inotify_read(int resolved, long buf, long count);
static int bfree_inotify_has_events(int resolved);
static int bfree_pidfd_is_valid_fd(int fd);
static void bfree_fanotify_release(int resolved);
static void bfree_perf_release(int resolved);
static void bfree_fsctx_release(int resolved);
static void bfree_iouring_release(int resolved);
static void bfree_uffd_release(int resolved);
static void bfree_landlock_release(int resolved);
static long sys_linux_fstat(long fd, long statbuf);
static uint16_t bfree_inet_ntohs(uint16_t x);
static uint32_t bfree_inet_ntohl(uint32_t x);
static int bfree_unix_from_fd(int fd);
static void bfree_sock_accept_q_reset(int *q_rd, int *q_wr, int *q_len,
                                      int *backlog);
static long bfree_unix_dgram_send(int idx, long buf, long len, long addr,
                                  long addrlen);
static long bfree_unix_dgram_recv(int idx, long buf, long len, int peek,
                                  int dontwait, long addr);
static void bfree_guest_alarm_poll(void);
static long bfree_coop_yield_to_parent_done(long ret);
static long bfree_coop_yield_to_child_done(long ret);
static void bfree_coop_fd_snap_init(void);
static void bfree_coop_fd_switch_to(int side);
static void bfree_coop_as_switch_to(int side);
static void bfree_coop_save_child_user(void);
static void bfree_coop_save_parent_user(void);
static void bfree_coop_publish_parent_resume(void);
static void bfree_coop_publish_child_resume(void);
static void bfree_coop_arm_parent_resume(void);
static long bfree_coop_yield_to_parent(void);
static long bfree_coop_yield_to_child(void);
static long bfree_guest_fork_enter(int copy_as);
static void bfree_guest_futex_wake_user(volatile int *uaddr);
static void bfree_guest_sig_raise(int sig);
static long bfree_linux_read_timerfd(long fd, long buf, long count);
static long bfree_linux_read_signalfd(long fd, long buf, long count);
static int bfree_guest_sig_take_eintr(void);
static long bfree_guest_sig_try_deliver(long ret);
static long bfree_guest_exit_from_fork_signal(int sig);
static void bfree_guest_flocks_drop_pid(int pid);
static int bfree_user_stack_poke_bytes(uint64_t user_vaddr, const char *bytes, uint64_t len);
static int bfree_user_stack_peek_bytes(uint64_t user_vaddr, char *bytes, uint64_t len);
static int bfree_user_stack_page_phys(uint64_t vaddr, uint64_t *out_phys);
static long sys_linux_poll_common(long fds_ptr, long nfds);
static long bfree_gthr_park_poll(long fds_ptr, long nfds);
static long bfree_gthr_park_futex(volatile int *uaddr, int val);
static long bfree_gthr_yield(void);
static long bfree_gthr_on_eventfd_write(void);
static long bfree_gthr_on_futex_wake(volatile int *uaddr, int want);

#ifndef BFREE_RESTORE_COOP_GLOBALS
#define BFREE_RESTORE_COOP_GLOBALS 1
static int g_coop_side;
static int g_coop_child_blocked;
static int g_coop_parent_started;
static uint64_t g_coop_child_rcx, g_coop_child_r11, g_coop_child_rsp;
static uint64_t g_coop_child_rbx, g_coop_child_rbp, g_coop_child_r12;
static uint64_t g_coop_child_r13, g_coop_child_r14, g_coop_child_r15, g_coop_child_rdx;
static int g_coop_session = -1;
static int g_guest_waitid_active;
static long g_guest_waitid_infop;
static long g_guest_wait_status_ptr;
static int g_coop_parent_in_wait; /* seq-fork: parent blocked in waitpid */
static int g_last_waitpid_status; /* for waitid after waitpid(status_ptr=0) */
static int g_guest_tty_pgrp = 1;
static int g_guest_sid = 1;
static uint8_t g_guest_sig_disp[BFREE_NSIG];
#ifndef BFREE_SA_SIGINFO
#define BFREE_SA_SIGINFO 0x4UL
#define BFREE_SA_NODEFER 0x40000000UL
#define BFREE_SA_ONSTACK 0x08000000UL /* Linux x86_64 */
#define BFREE_SS_ONSTACK 1
#define BFREE_SS_DISABLE 2
#define BFREE_SS_AUTODISARM 0x80000000UL /* accepted, not enforced (H17 residual) */
#define BFREE_MINSIGSTKSZ 2048
#endif
static void *g_guest_sig_handler[BFREE_NSIG];
static void *g_guest_sig_restorer[BFREE_NSIG];
static unsigned long g_guest_sig_flags[BFREE_NSIG];
static uint64_t g_guest_sig_sa_mask[BFREE_NSIG];
static uint64_t g_guest_sig_pending;
static uint64_t g_guest_sig_mask;
#endif
/* === end restore compile glue === */

static void preempt_disable(void);
static void preempt_enable(void);
static int g_guest_preempt_count;
static volatile int g_guest_need_resched;

/* ---- Cooperative multi-thread (Linux-like wait / wake / switch) ---- */
#define BFREE_GTHR_UNUSED   0
#define BFREE_GTHR_RUNNING  1
#define BFREE_GTHR_RUNNABLE 2
#define BFREE_GTHR_WAIT_POLL  3
#define BFREE_GTHR_WAIT_FUTEX 4
#define BFREE_GTHR_WAIT_SIGWAIT 5

typedef struct {
    int used;
    int tid;
    int state;
    uint64_t rcx, r11, rsp, rbx, rbp, r12, r13, r14, r15, rdx, rax, fsbase;
    long poll_fds;
    long poll_nfds;
    volatile int *futex_uaddr;
    int futex_val;
    uintptr_t clear_child_tid;
    uint64_t sigwait_want;
    uint64_t sigwait_deadline_us; /* 0 = infinite */
    long sigwait_uinfo;
} bfree_gthr_t;

static bfree_gthr_t g_gthr[BFREE_GUEST_MAX_THREADS];
static int g_gthr_cur = -1;
static int g_gthr_main;

static void bfree_guest_thread_init(void)
{
    int i;

    g_guest_thread_active = 0;
    g_guest_thread_tid = 0;
    g_guest_thread_slots_used = 0;
    g_gthr_cur = -1;
    g_gthr_main = 0;
    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        g_gthr[i].used = 0;
        g_gthr[i].state = BFREE_GTHR_UNUSED;
    }
}

static int bfree_gthr_mt(void)
{
    return g_gthr_cur >= 0;
}

static int bfree_gthr_count_used(void)
{
    int i;
    int n = 0;

    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (g_gthr[i].used) {
            ++n;
        }
    }
    return n;
}

static void bfree_gthr_save_current(void)
{
    bfree_gthr_t *t;

    if (g_gthr_cur < 0 || g_gthr_cur >= BFREE_GUEST_MAX_THREADS) {
        return;
    }
    t = &g_gthr[g_gthr_cur];
    t->rcx = g_bfree_user_sysret_rcx;
    t->r11 = g_bfree_user_sysret_r11;
    t->rsp = g_bfree_user_sysret_rsp;
    t->rbx = g_bfree_user_sysret_rbx;
    t->rbp = g_bfree_user_sysret_rbp;
    t->r12 = g_bfree_user_sysret_r12;
    t->r13 = g_bfree_user_sysret_r13;
    t->r14 = g_bfree_user_sysret_r14;
    t->r15 = g_bfree_user_sysret_r15;
    t->rdx = g_bfree_user_sysret_rdx;
    t->fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
}

static int bfree_gthr_poll_ready(const bfree_gthr_t *t)
{
    if (t->state != BFREE_GTHR_WAIT_POLL) {
        return 0;
    }
    return sys_linux_poll_common(t->poll_fds, t->poll_nfds) > 0;
}

static int bfree_gthr_futex_ready(const bfree_gthr_t *t)
{
    if (t->state != BFREE_GTHR_WAIT_FUTEX || t->futex_uaddr == 0) {
        return 0;
    }
    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)t->futex_uaddr)) {
        return 1;
    }
    return *(volatile int *)t->futex_uaddr != t->futex_val;
}

static int bfree_gthr_sigwait_pick(uint64_t want, long uinfo)
{
    uint64_t hit = g_guest_sig_pending & want;
    int sig;

    if (hit == 0ULL) {
        return 0;
    }
    for (sig = 1; sig < BFREE_NSIG; ++sig) {
        uint64_t bit = 1ULL << (unsigned)(sig - 1);

        if ((hit & bit) == 0ULL) {
            continue;
        }
        g_guest_sig_pending &= ~bit;
        if (uinfo != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)uinfo)) {
            int *si = (int *)(uintptr_t)uinfo;

            si[0] = sig;
            si[1] = 0;
            si[2] = 0;
        }
        return sig;
    }
    return 0;
}

static int bfree_gthr_sigwait_ready(const bfree_gthr_t *t)
{
    if (t->state != BFREE_GTHR_WAIT_SIGWAIT) {
        return 0;
    }
    if ((g_guest_sig_pending & t->sigwait_want) != 0ULL) {
        return 1;
    }
    if (t->sigwait_deadline_us != 0ULL &&
        knl_get_current_time() >= t->sigwait_deadline_us) {
        return 1;
    }
    return 0;
}

static void bfree_gthr_sigwait_complete(bfree_gthr_t *t)
{
    int sig;

    if (t->state != BFREE_GTHR_WAIT_SIGWAIT) {
        return;
    }
    sig = bfree_gthr_sigwait_pick(t->sigwait_want, t->sigwait_uinfo);
    if (sig > 0) {
        t->rax = (uint64_t)(long)sig;
    } else {
        t->rax = (uint64_t)(long)-110; /* ETIMEDOUT */
    }
    t->sigwait_want = 0;
    t->sigwait_deadline_us = 0;
    t->sigwait_uinfo = 0;
    t->state = BFREE_GTHR_RUNNABLE;
}

static void bfree_gthr_wake_sigwait(int sig)
{
    int i;
    uint64_t bit;

    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    bit = 1ULL << (unsigned)(sig - 1);
    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        bfree_gthr_t *t = &g_gthr[i];

        if (!t->used || t->state != BFREE_GTHR_WAIT_SIGWAIT) {
            continue;
        }
        if ((t->sigwait_want & bit) == 0ULL) {
            continue;
        }
        bfree_gthr_sigwait_complete(t);
    }
}

static int bfree_gthr_find_runnable(int except)
{
    int i;

    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (i == except || !g_gthr[i].used) {
            continue;
        }
        if (g_gthr[i].state == BFREE_GTHR_RUNNABLE) {
            return i;
        }
    }
    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (i == except || !g_gthr[i].used) {
            continue;
        }
        if (bfree_gthr_poll_ready(&g_gthr[i]) || bfree_gthr_futex_ready(&g_gthr[i]) ||
            bfree_gthr_sigwait_ready(&g_gthr[i])) {
            return i;
        }
    }
    return -1;
}

static long bfree_gthr_publish_switch(int to_idx)
{
    bfree_gthr_t *t;

    if (to_idx < 0 || to_idx >= BFREE_GUEST_MAX_THREADS || !g_gthr[to_idx].used) {
        return 0;
    }
    t = &g_gthr[to_idx];
    if (t->state == BFREE_GTHR_WAIT_POLL) {
        long ready = sys_linux_poll_common(t->poll_fds, t->poll_nfds);

        if (ready <= 0) {
            return 0;
        }
        t->rax = (uint64_t)ready;
        t->poll_fds = 0;
    } else if (t->state == BFREE_GTHR_WAIT_FUTEX) {
        if (!bfree_gthr_futex_ready(t)) {
            return 0;
        }
        t->rax = 0;
    } else if (t->state == BFREE_GTHR_WAIT_SIGWAIT) {
        if (!bfree_gthr_sigwait_ready(t)) {
            return 0;
        }
        bfree_gthr_sigwait_complete(t);
    }

    g_gthr_cur = to_idx;
    t->state = BFREE_GTHR_RUNNING;
    g_guest_thread_tid = t->tid;
    g_guest_thread_active = (bfree_gthr_count_used() > 1) ? 1 : 0;
    g_guest_clear_child_tid = t->clear_child_tid;
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, t->fsbase);

    g_bfree_fork_saved_rcx = t->rcx;
    g_bfree_fork_saved_r11 = t->r11;
    g_bfree_fork_saved_rsp = t->rsp;
    g_bfree_fork_saved_rbx = t->rbx;
    g_bfree_fork_saved_rbp = t->rbp;
    g_bfree_fork_saved_r12 = t->r12;
    g_bfree_fork_saved_r13 = t->r13;
    g_bfree_fork_saved_r14 = t->r14;
    g_bfree_fork_saved_r15 = t->r15;
    g_bfree_fork_saved_rdx = t->rdx;
    g_bfree_fork_parent_ret = t->rax;
    g_guest_fork_saved_fsbase = t->fsbase;
    return BFREE_SYSRET_THREAD_SWITCH;
}

static long bfree_gthr_park_poll(long fds_ptr, long nfds)
{
    int other;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    other = bfree_gthr_find_runnable(g_gthr_cur);
    if (other < 0) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].state = BFREE_GTHR_WAIT_POLL;
    g_gthr[g_gthr_cur].poll_fds = fds_ptr;
    g_gthr[g_gthr_cur].poll_nfds = nfds;
    g_gthr[g_gthr_cur].futex_uaddr = 0;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static long bfree_gthr_yield(void)
{
    int other;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    other = bfree_gthr_find_runnable(g_gthr_cur);
    if (other < 0) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].rax = 0;
    g_gthr[g_gthr_cur].state = BFREE_GTHR_RUNNABLE;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static long bfree_gthr_park_futex(volatile int *uaddr, int val)
{
    int other;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    other = bfree_gthr_find_runnable(g_gthr_cur);
    if (other < 0) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].state = BFREE_GTHR_WAIT_FUTEX;
    g_gthr[g_gthr_cur].futex_uaddr = uaddr;
    g_gthr[g_gthr_cur].futex_val = val;
    g_gthr[g_gthr_cur].poll_fds = 0;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static long bfree_gthr_park_sigwait(uint64_t want, uint64_t deadline_us, long uinfo)
{
    int other;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    other = bfree_gthr_find_runnable(g_gthr_cur);
    if (other < 0) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].state = BFREE_GTHR_WAIT_SIGWAIT;
    g_gthr[g_gthr_cur].sigwait_want = want;
    g_gthr[g_gthr_cur].sigwait_deadline_us = deadline_us;
    g_gthr[g_gthr_cur].sigwait_uinfo = uinfo;
    g_gthr[g_gthr_cur].futex_uaddr = 0;
    g_gthr[g_gthr_cur].poll_fds = 0;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static long bfree_gthr_on_eventfd_write(void)
{
    int i;
    int woke = 0;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    /* Mark poll waiters RUNNABLE but do NOT steal: if we switch to a QThread
     * that never parks again, the waker (often pthread_create) never returns. */
    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (i == g_gthr_cur || !g_gthr[i].used) {
            continue;
        }
        if (g_gthr[i].state == BFREE_GTHR_WAIT_POLL) {
            g_gthr[i].state = BFREE_GTHR_RUNNABLE;
            g_gthr[i].rax = 1;
            g_gthr[i].poll_fds = 0;
            g_gthr[i].poll_nfds = 0;
            ++woke;
        }
    }
    (void)woke;
    return 0;
}

static long bfree_gthr_on_futex_wake(volatile int *uaddr, int want)
{
    int i;
    int woke = 0;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    /* WAKE = make waiters RUNNABLE only. Stealing to the waiter hung desktop
     * pthread_create: child ran QThread ppoll forever and parent never left WAKE. */
    for (i = 0; i < BFREE_GUEST_MAX_THREADS && woke < want; ++i) {
        if (!g_gthr[i].used || g_gthr[i].state != BFREE_GTHR_WAIT_FUTEX) {
            continue;
        }
        if (g_gthr[i].futex_uaddr != uaddr) {
            continue;
        }
        g_gthr[i].state = BFREE_GTHR_RUNNABLE;
        g_gthr[i].rax = 0;
        g_gthr[i].futex_uaddr = 0;
        ++woke;
    }
    return woke > 0 ? (long)woke : 0;
}

static void bfree_guest_thread_save_parent_ctx(void)
{
    g_bfree_fork_saved_rcx = g_bfree_user_sysret_rcx;
    g_bfree_fork_saved_r11 = g_bfree_user_sysret_r11;
    g_bfree_fork_saved_rsp = g_bfree_user_sysret_rsp;
    g_bfree_fork_saved_rbx = g_bfree_user_sysret_rbx;
    g_bfree_fork_saved_rbp = g_bfree_user_sysret_rbp;
    g_bfree_fork_saved_r12 = g_bfree_user_sysret_r12;
    g_bfree_fork_saved_r13 = g_bfree_user_sysret_r13;
    g_bfree_fork_saved_r14 = g_bfree_user_sysret_r14;
    g_bfree_fork_saved_r15 = g_bfree_user_sysret_r15;
    g_bfree_fork_saved_rdx = g_bfree_user_sysret_rdx;
}

static long bfree_guest_thread_clone(unsigned long flags, long newsp, long ptid, long ctid, long tls)
{
    int tid;
    int child_idx = -1;
    int i;
    uint64_t child_rsp;
    uint64_t parent_fs;
    bfree_gthr_t *parent;
    bfree_gthr_t *child;

    preempt_disable();
    /* Allow CLONE_THREAD inside a forked child after execve (busybox shell
     * leaves g_guest_fork_active set). Coop process-fork still uses other paths.
     * Blocking here made early pthread/clone in a freshly exec'd ELF return EAGAIN
     * while late curated thread_clone_join passed after fork tests cleared the flag.
     */
    if (newsp == 0 || !bfree_user_ptr_mapped(newsp)) {
        preempt_enable();
        return -14;
    }
    child_rsp = (uint64_t)(uintptr_t)newsp;
    child_rsp &= ~0xFULL;
    /* SysV AMD64: function entry wants RSP%16==8 (as after CALL). */
    child_rsp -= 8ULL;

    tid = g_guest_next_pid++;
    if (tid <= 0) {
        preempt_enable();
        return -11;
    }

    /* First clone: register main as slot 0 (stays runnable with rax=tid). */
    if (!bfree_gthr_mt()) {
        g_gthr_main = 0;
        g_gthr_cur = 0;
        parent = &g_gthr[0];
        parent->used = 1;
        parent->tid = 1;
        parent->state = BFREE_GTHR_RUNNABLE;
        parent->rcx = g_bfree_user_sysret_rcx;
        parent->r11 = g_bfree_user_sysret_r11;
        parent->rsp = g_bfree_user_sysret_rsp;
        parent->rbx = g_bfree_user_sysret_rbx;
        parent->rbp = g_bfree_user_sysret_rbp;
        parent->r12 = g_bfree_user_sysret_r12;
        parent->r13 = g_bfree_user_sysret_r13;
        parent->r14 = g_bfree_user_sysret_r14;
        parent->r15 = g_bfree_user_sysret_r15;
        parent->rdx = g_bfree_user_sysret_rdx;
        parent->fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
        parent->rax = (uint64_t)(long)tid;
        parent->clear_child_tid = 0;
        parent->poll_fds = 0;
        parent->futex_uaddr = 0;
        parent->sigwait_want = 0;
        parent->sigwait_deadline_us = 0;
        parent->sigwait_uinfo = 0;
        g_guest_thread_slots_used = 1;
    } else {
        bfree_gthr_save_current();
        g_gthr[g_gthr_cur].state = BFREE_GTHR_RUNNABLE;
        g_gthr[g_gthr_cur].rax = (uint64_t)(long)tid;
    }

    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (!g_gthr[i].used) {
            child_idx = i;
            break;
        }
    }
    if (child_idx < 0) {
        preempt_enable();
        return -11;
    }

    parent_fs = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
    g_guest_fork_saved_fsbase = parent_fs;
    bfree_guest_thread_save_parent_ctx();

    child = &g_gthr[child_idx];
    child->used = 1;
    child->tid = tid;
    child->state = BFREE_GTHR_RUNNING;
    child->rcx = g_bfree_user_sysret_rcx;
    child->r11 = g_bfree_user_sysret_r11;
    child->rsp = child_rsp;
    child->rbx = g_bfree_user_sysret_rbx;
    child->rbp = g_bfree_user_sysret_rbp;
    child->r12 = g_bfree_user_sysret_r12;
    child->r13 = g_bfree_user_sysret_r13;
    child->r14 = g_bfree_user_sysret_r14;
    child->r15 = g_bfree_user_sysret_r15;
    child->rdx = g_bfree_user_sysret_rdx;
    child->rax = 0;
    child->fsbase = parent_fs;
    child->clear_child_tid = 0;
    child->poll_fds = 0;
    child->futex_uaddr = 0;
    child->sigwait_want = 0;
    child->sigwait_deadline_us = 0;
    child->sigwait_uinfo = 0;

    if ((flags & 0x00080000UL) != 0UL && tls != 0) { /* CLONE_SETTLS */
        if (!bfree_user_ptr_mapped(tls)) {
            child->used = 0;
            preempt_enable();
            return -14;
        }
        child->fsbase = (uint64_t)(uintptr_t)tls;
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, child->fsbase);
    }

    if ((flags & 0x00100000UL) != 0UL && ptid != 0 && bfree_user_ptr_mapped(ptid)) {
        *(int *)(uintptr_t)ptid = tid;
    }
    if ((flags & 0x00200000UL) != 0UL && ctid != 0 && bfree_user_ptr_mapped(ctid)) {
        child->clear_child_tid = (uintptr_t)ctid;
        g_guest_clear_child_tid = (uintptr_t)ctid;
        *(int *)(uintptr_t)ctid = tid;
    } else {
        g_guest_clear_child_tid = 0;
    }

    g_gthr_cur = child_idx;
    g_guest_thread_active = 1;
    g_guest_thread_tid = tid;
    g_guest_thread_slots_used = bfree_gthr_count_used();
    g_guest_need_resched = 0;

    g_bfree_sysret_exec_rsp = child_rsp;
    preempt_enable();
    return BFREE_SYSRET_THREAD_CHILD;
}

static long bfree_guest_thread_exit(long status)
{
    int tid;
    int *cleartid;
    int other;
    uintptr_t ctid;
    int idx;
    int was_main;

    (void)status;
    preempt_disable();
    if (!bfree_gthr_mt()) {
        preempt_enable();
        return -1;
    }
    idx = g_gthr_cur;
    if (idx < 0 || !g_gthr[idx].used) {
        preempt_enable();
        return -1;
    }
    tid = g_gthr[idx].tid;
    ctid = g_gthr[idx].clear_child_tid;
    was_main = (idx == g_gthr_main);

    if (ctid != 0 && bfree_user_ptr_mapped((long)ctid)) {
        int wi;

        cleartid = (int *)(uintptr_t)ctid;
        *cleartid = 0;
        bfree_guest_futex_wake_user((volatile int *)cleartid);
        for (wi = 0; wi < BFREE_GUEST_MAX_THREADS; ++wi) {
            if (!g_gthr[wi].used || g_gthr[wi].state != BFREE_GTHR_WAIT_FUTEX) {
                continue;
            }
            if (g_gthr[wi].futex_uaddr != (volatile int *)cleartid) {
                continue;
            }
            g_gthr[wi].state = BFREE_GTHR_RUNNABLE;
            g_gthr[wi].rax = 0;
            g_gthr[wi].futex_uaddr = 0;
        }
    }

    g_gthr[idx].used = 0;
    g_gthr[idx].state = BFREE_GTHR_UNUSED;
    g_guest_thread_slots_used = bfree_gthr_count_used();

    /* Main thread exit ends the whole guest process group. */
    if (was_main) {
        int i;

        for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
            g_gthr[i].used = 0;
            g_gthr[i].state = BFREE_GTHR_UNUSED;
        }
        g_guest_thread_active = 0;
        g_guest_thread_tid = 0;
        g_guest_thread_slots_used = 0;
        g_gthr_cur = -1;
        preempt_enable();
        /* Fall through to process exit (busybox re-enter / halt path). */
        return -1;
    }

    other = bfree_gthr_find_runnable(idx);
    if (other < 0) {
        /* Fall back to main if still present. */
        if (g_gthr[g_gthr_main].used) {
            other = g_gthr_main;
            g_gthr[other].state = BFREE_GTHR_RUNNABLE;
        }
    }
    if (other < 0) {
        g_guest_thread_active = 0;
        g_guest_thread_tid = 0;
        g_gthr_cur = -1;
        preempt_enable();
        return -1;
    }
    if (g_guest_thread_slots_used <= 1 && other == g_gthr_main) {
        g_guest_thread_active = 0;
    }
    {
        long sw = bfree_gthr_publish_switch(other);
        preempt_enable();
        return sw != 0 ? sw : BFREE_SYSRET_THREAD_SWITCH;
    }
}

/* A: ENOSYS appearance tracer — log first hit + histogram (no mass-fill). */
#define BFREE_ENOSYS_HIST 48
static uint16_t g_enosys_nr[BFREE_ENOSYS_HIST];
static uint32_t g_enosys_hit[BFREE_ENOSYS_HIST];
static uint32_t g_enosys_total;
static uint32_t g_enosys_slots;

static void bfree_enosys_note(long num)
{
    unsigned i;
    uint32_t c = 0;

    if (num < 0 || num > 65535) {
        return;
    }
    g_enosys_total++;
    for (i = 0; i < g_enosys_slots; ++i) {
        if (g_enosys_nr[i] == (uint16_t)num) {
            g_enosys_hit[i]++;
            c = g_enosys_hit[i];
            break;
        }
    }
    if (c == 0 && g_enosys_slots < BFREE_ENOSYS_HIST) {
        i = g_enosys_slots++;
        g_enosys_nr[i] = (uint16_t)num;
        g_enosys_hit[i] = 1;
        c = 1;
    }
    /* First appearance, then every 8th — keeps UART readable. */
    if (c == 1U || (c & 7U) == 0U) {
        uart_puts("[ENOSYS] nr=");
        uart_puthex64((uint64_t)(unsigned long)num);
        uart_puts(" count=");
        uart_puthex64((uint64_t)c);
        uart_puts(" total=");
        uart_puthex64((uint64_t)g_enosys_total);
        uart_puts("\n");
    }
}

/* Nestable barriers for guest thread/TLS/futex critical sections.
 * No timer-driven guest preemption yet; counter is for future IRQ path. */
/* g_guest_preempt_count / g_guest_need_resched declared above gthr block. */

static void preempt_disable(void)
{
    g_guest_preempt_count++;
}

static void preempt_enable(void)
{
    if (g_guest_preempt_count > 0) {
        g_guest_preempt_count--;
    }
}

/* Declared early so cooperative fork can snapshot/restore it. */
static char g_guest_cwd[256] = "/";
/* Kernel-side cwd is not in the shared user address space, so without an
 * explicit snapshot a cooperative child `chdir` would permanently move the
 * parent shell. Save/restore gives fork-like cwd isolation for ash subshells. */
static char g_guest_fork_saved_cwd[256];
static uint64_t g_guest_fork_saved_heap_next;
static uint64_t g_guest_fork_saved_brk;
/* AS-copy: kernel brk/cwd are global. Dual-park across coop switches so each
 * side resumes its own markers. On child exit restore parent park (not
 * fork-enter) so post-fork parent brk is not rewound. First child resume uses
 * fork-enter (birth heap) until the child gets its own park slot. */
static int g_guest_parent_parked_heap_valid;
static uint64_t g_guest_parent_parked_heap_next;
static uint64_t g_guest_parent_parked_brk;
static char g_guest_parent_parked_cwd[256];
static int g_guest_child_parked_heap_valid;
static uint64_t g_guest_child_parked_heap_next;
static uint64_t g_guest_child_parked_brk;
static char g_guest_child_parked_cwd[256];

static void bfree_guest_cwd_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i;

    for (i = 0; i < dst_sz; ++i) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }
    if (dst_sz > 0U) {
        dst[dst_sz - 1U] = '\0';
    }
}

static void bfree_guest_load_heap_cwd(const char *cwd_src, uint64_t heap_src, uint64_t brk_src)
{
    bfree_guest_cwd_copy(g_guest_cwd, sizeof(g_guest_cwd), cwd_src);
    g_guest_heap_next = heap_src;
    g_guest_brk = brk_src;
}

static void bfree_guest_park_heap_side(int child_side)
{
    if (child_side) {
        g_guest_child_parked_heap_next = g_guest_heap_next;
        g_guest_child_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_child_parked_cwd, sizeof(g_guest_child_parked_cwd),
                             g_guest_cwd);
        g_guest_child_parked_heap_valid = 1;
    } else {
        g_guest_parent_parked_heap_next = g_guest_heap_next;
        g_guest_parent_parked_brk = g_guest_brk;
        bfree_guest_cwd_copy(g_guest_parent_parked_cwd, sizeof(g_guest_parent_parked_cwd),
                             g_guest_cwd);
        g_guest_parent_parked_heap_valid = 1;
    }
}

static void bfree_guest_as_copy_switch_heap_to_child(void)
{
    bfree_guest_park_heap_side(0);
    if (g_guest_child_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_child_parked_cwd,
                                  g_guest_child_parked_heap_next,
                                  g_guest_child_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
}

static void bfree_guest_as_copy_switch_heap_to_parent(void)
{
    bfree_guest_park_heap_side(1);
    if (g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    }
}

static void bfree_guest_restore_parent_isol(int as_copy)
{
    if (as_copy && g_guest_parent_parked_heap_valid) {
        bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                  g_guest_parent_parked_heap_next,
                                  g_guest_parent_parked_brk);
    } else {
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
}

/* Dual-live session stubs (full H02 not recovered). */
static void bfree_coop_session_park_globals(int sess)
{
    (void)sess;
}

static void bfree_coop_sessions_init(void)
{
    g_coop_session = -1;
}

/* Parent stack snapshot: child returns from forkshell before exec and reuses
 * the shared stack, trashing the frozen parent's frame (jp etc.). */
#define BFREE_VFORK_STACK_SAVE_PAGES 16
#define BFREE_VFORK_STACK_SAVE_BYTES ((uint64_t)BFREE_VFORK_STACK_SAVE_PAGES * PAGE_SIZE)
static uint8_t g_guest_fork_stack_save[BFREE_VFORK_STACK_SAVE_BYTES] __attribute__((aligned(16)));
static uint64_t g_guest_fork_stack_save_base;
static int g_guest_fork_stack_save_valid;

/* Minimal Linux waitid / SIGCHLD constants (used by exit-from-fork too). */
#define BFREE_P_ALL  0
#define BFREE_P_PID  1
#define BFREE_P_PGID 2
#define BFREE_WNOHANG_ID 0x00000001
#define BFREE_WEXITED    0x00000004
#define BFREE_SIGCHLD    17
#define BFREE_CLD_EXITED 1
#define BFREE_CLD_KILLED 2

typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int __pad0;
    int si_pid;
    unsigned int si_uid;
    int si_status;
    long si_utime;
    long si_stime;
} bfree_siginfo_wait_t;

static int bfree_guest_vfork_stack_snapshot(uint64_t rsp);
static void bfree_guest_vfork_stack_restore(void);

static int bfree_user_ptr_mapped(long ptr);
static int bfree_user_buf_mapped(uint64_t base, uint64_t len);
static void bfree_guest_stdio_heal_pipes(void);
static void bfree_guest_pipe_reclaim_dead_slots(void);
static long sys_linux_pipe2(long pipefd_ptr, long flags);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static uint64_t bfree_rdmsr64(uint32_t msr);
#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif

/* dup removed: bfree_guest_thread_init */

/* dup removed: bfree_guest_thread_save_parent_ctx */

/* dup removed: bfree_guest_thread_clone */

/* dup removed: bfree_guest_thread_exit */

void bfree_sysret_exec_globals_init(void)
{
    g_bfree_sysret_exec_rsp = 0;
    g_bfree_sysret_exec_rcx = 0;
    g_bfree_sysret_exec_r11 = 0;
    g_bfree_sysret_exec_rdi = 0;
    g_bfree_sysret_exec_rsi = 0;
    g_bfree_sysret_exec_rdx = 0;
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_exec_transfer_rip = 0;
    g_bfree_sysret_sig_rax = 0;
    g_bfree_user_sysret_rcx = 0;
    g_bfree_user_sysret_r11 = 0;
    g_bfree_user_sysret_rsp = 0;
    g_bfree_user_sysret_rbx = 0;
    g_bfree_user_sysret_rbp = 0;
    g_bfree_user_sysret_r12 = 0;
    g_bfree_user_sysret_r13 = 0;
    g_bfree_user_sysret_r14 = 0;
    g_bfree_user_sysret_r15 = 0;
    g_bfree_user_sysret_rdx = 0;
    g_bfree_fork_parent_ret = 0;
    g_bfree_fork_saved_rcx = 0;
    g_bfree_fork_saved_r11 = 0;
    g_bfree_fork_saved_rsp = 0;
    g_bfree_fork_saved_rbx = 0;
    g_bfree_fork_saved_rbp = 0;
    g_bfree_fork_saved_r12 = 0;
    g_bfree_fork_saved_r13 = 0;
    g_bfree_fork_saved_r14 = 0;
    g_bfree_fork_saved_r15 = 0;
    g_bfree_fork_saved_rdx = 0;
    g_guest_fork_saved_fsbase = 0;
    g_guest_fork_stack_save_base = 0;
    g_guest_fork_stack_save_valid = 0;
    g_guest_fork_active = 0;
    g_guest_fork_pid = 0;
    g_guest_fork_status = 0;
    g_guest_fork_status_ready = 0;
    g_guest_fork_was_as_copy = 0;
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
    g_guest_next_pid = 2;
    bfree_process_init();
}

static long bfree_guest_fork_enter(int copy_as)
{
    size_t i;
    int child_pid = 0;
    long rc;

    /* Snapshot parent return point before child syscalls clobber user_sysret_*. */
    g_bfree_fork_saved_rcx = g_bfree_user_sysret_rcx;
    g_bfree_fork_saved_r11 = g_bfree_user_sysret_r11;
    g_bfree_fork_saved_rsp = g_bfree_user_sysret_rsp;
    g_bfree_fork_saved_rbx = g_bfree_user_sysret_rbx;
    g_bfree_fork_saved_rbp = g_bfree_user_sysret_rbp;
    g_bfree_fork_saved_r12 = g_bfree_user_sysret_r12;
    g_bfree_fork_saved_r13 = g_bfree_user_sysret_r13;
    g_bfree_fork_saved_r14 = g_bfree_user_sysret_r14;
    g_bfree_fork_saved_r15 = g_bfree_user_sysret_r15;
    g_bfree_fork_saved_rdx = g_bfree_user_sysret_rdx;
    /* Seed parent park so later COOP publish works after AS-copy parent-first. */
    g_coop_parent_rcx = g_bfree_user_sysret_rcx;
    g_coop_parent_r11 = g_bfree_user_sysret_r11;
    g_coop_parent_rsp = g_bfree_user_sysret_rsp;
    g_coop_parent_rbx = g_bfree_user_sysret_rbx;
    g_coop_parent_rbp = g_bfree_user_sysret_rbp;
    g_coop_parent_r12 = g_bfree_user_sysret_r12;
    g_coop_parent_r13 = g_bfree_user_sysret_r13;
    g_coop_parent_r14 = g_bfree_user_sysret_r14;
    g_coop_parent_r15 = g_bfree_user_sysret_r15;
    g_coop_parent_rdx = g_bfree_user_sysret_rdx;
    g_guest_fork_saved_fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    for (i = 0; i < sizeof(g_guest_fork_saved_cwd); ++i) {
        g_guest_fork_saved_cwd[i] = g_guest_cwd[i];
        if (g_guest_cwd[i] == '\0') {
            break;
        }
    }
    g_guest_fork_saved_cwd[sizeof(g_guest_fork_saved_cwd) - 1U] = '\0';
    g_guest_fork_saved_heap_next = g_guest_heap_next;
    g_guest_fork_saved_brk = g_guest_brk;
    if (!copy_as) {
        (void)bfree_guest_vfork_stack_snapshot(g_bfree_fork_saved_rsp);
    }

    if (copy_as) {
        /* EAGAIN if another live AS-copy/vfork child already occupies a slot. */
        rc = bfree_process_fork_enter(&child_pid);
    } else {
        rc = bfree_process_vfork_enter(&child_pid);
    }
    if (rc < 0) {
        return rc;
    }
    g_guest_fork_pid = child_pid;
    g_guest_fork_active = 1;
    g_guest_fork_status_ready = 0;
    g_guest_fork_was_as_copy = copy_as ? 1 : 0;
    g_guest_parent_parked_heap_valid = 0;
    g_guest_child_parked_heap_valid = 0;
    bfree_coop_fd_snap_init();
    /* Fork duplicates fds: recount so each end is held by parent+child. */
    bfree_guest_pipe_reclaim_dead_slots();
    if (copy_as) {
        /*
         * AS-copy: return to parent immediately (parent-first). Child parked
         * until parent blocks on stdin/wait/pipe and yields. CR3 flipped in C;
         * FORK_PARENT/COOP must not consume g_bfree_sysret_exec_cr3.
         */
        bfree_coop_save_child_user();
        g_coop_child_blocked = 1;
        bfree_coop_fd_switch_to(0);
        bfree_coop_as_switch_to(0);
        g_coop_parent_started = 1;
        g_bfree_sysret_exec_cr3 = 0;
        bfree_coop_publish_parent_resume();
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
        return BFREE_SYSRET_COOP_SWITCH;
    }
    return 0; /* vfork: continue as child; parent resumes on child exit */
}


static long bfree_guest_exit_from_fork(long status)
{
    int *cleartid;
    size_t i;

    int as_copy = g_guest_fork_was_as_copy;
    /* Capture before exit_restore_as clears parent_pt / has_private_as. */
    page_table_t *resume_pt = bfree_process_parent_pt();
    if (!resume_pt && knl_current_task) {
        resume_pt = (page_table_t *)knl_current_task->page_table_base;
    }

    bfree_process_exit_child((int)status);
    bfree_guest_flocks_drop_pid(g_guest_fork_pid);
    g_guest_fork_active = 0;
    g_guest_fork_status = (int)(status & 0xff);
    g_guest_fork_status_ready = 1;
    /* Shared fd table: a pipeline child may leave stdin/stdout wired to a
     * pipe. Restore the shell's console before the parent resumes. */
    bfree_guest_sig_raise(17);
    bfree_coop_fd_switch_to(0);
    g_coop_child_blocked = 0;
    /* Fork is over: stale snaps must not pin OFDs/pipes (slot exhaustion). */
    {
        int si;
        for (si = 0; si < BFREE_GUEST_FD_TABLE_SIZE; ++si) {
            g_fd_snap_parent[si] = -1;
            g_fd_snap_child[si] = -1;
            g_fd_dup_save_snap_parent[si] = -1;
            g_fd_dup_save_snap_child[si] = -1;
        }
    }
    bfree_guest_stdio_heal_pipes();
    /* Parent stack live in parent_pt — must restore before vfork stack poke.
     * After vfork+exec the TCB still points at child_pt. */
    if (resume_pt && knl_current_task) {
        knl_current_task->page_table_base = resume_pt;
        __asm__ volatile("mov %0, %%cr3" :: "r"(resume_pt) : "memory");
    }
    /* AS-copy: parent kept running — do not rewind heap/stack. */
    if (!as_copy) {
        for (i = 0; i < sizeof(g_guest_cwd); ++i) {
            g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
            if (g_guest_fork_saved_cwd[i] == '\0') {
                break;
            }
        }
        g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\0';
        g_guest_heap_next = g_guest_fork_saved_heap_next;
        g_guest_brk = g_guest_fork_saved_brk;
        bfree_guest_vfork_stack_restore();
    } else {
        bfree_guest_restore_parent_isol(1);
        bfree_coop_as_switch_to(0);
    }
    g_guest_fork_was_as_copy = 0;
    /* Child execve cleared FS/TLS; restore the frozen parent's TLS base. */
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    g_bfree_sysret_exec_cr3 = 0;
    if (g_guest_clear_child_tid != 0 &&
        bfree_user_ptr_mapped((long)g_guest_clear_child_tid)) {
        cleartid = (int *)(uintptr_t)g_guest_clear_child_tid;
        *cleartid = 0;
    }
    /*
     * Parent may have been mid-syscall (pipe write/wait) when it yielded to the
     * child. publish_child_resume overwrote fork_saved_* with the child frame;
     * restore the frozen parent frame before FORK_PARENT sysret.
     *
     * Mid-waitpid must complete as mode-2 (post-syscall RIP + wait result):
     * yield_to_child arms mode-1 (restart), but child exit clobbers RDI/RSI so
     * a restart is unsafe. Reap + write *status here, then arm mode-2.
     */
    {
        int parent_waiting =
            (g_coop_parent_in_wait || g_guest_wait_status_ptr != 0 ||
             g_guest_waitid_active) ? 1 : 0;
        int st = 0;
        long wr = 0;

        /* Ensure status write hits the parent's stack pages. */
        if (resume_pt && knl_current_task) {
            knl_current_task->page_table_base = resume_pt;
            __asm__ volatile("mov %0, %%cr3" :: "r"(resume_pt) : "memory");
        }

        if (parent_waiting) {
            wr = bfree_process_wait4(
                g_guest_fork_pid > 0 ? (long)g_guest_fork_pid : -1L, &st, 0);
            if (g_guest_wait_status_ptr != 0) {
                /* Stack slot from waitpid; do not depend on walk during AS churn. */
                *(int *)(uintptr_t)g_guest_wait_status_ptr =
                    (wr > 0) ? st : (((int)(status & 0xff)) << 8);
            }
            if (g_guest_waitid_active && g_guest_waitid_infop != 0 &&
                bfree_user_ptr_mapped(g_guest_waitid_infop)) {
                uint8_t *raw = (uint8_t *)(uintptr_t)g_guest_waitid_infop;
                size_t i;
                int use_st = (wr > 0) ? st : (((int)(status & 0xff)) << 8);
                long use_pid = (wr > 0) ? wr : (long)g_guest_fork_pid;
                for (i = 0; i < 32; ++i) {
                    raw[i] = 0;
                }
                *(int *)(void *)(raw + 0) = BFREE_SIGCHLD;
                *(int *)(void *)(raw + 8) = BFREE_CLD_EXITED;
                *(int *)(void *)(raw + 16) = (int)use_pid;
                *(int *)(void *)(raw + 24) = (use_st >> 8) & 0xff;
            }
            uart_puts("[VFORK] wait reap wr=");
            uart_puthex64((uint64_t)(unsigned long)wr);
            uart_puts(" st=");
            uart_puthex64((uint64_t)(unsigned)(unsigned int)st);
            uart_puts("\n");
        }

        if (as_copy && g_coop_parent_started) {
            bfree_coop_publish_parent_resume();
            if (parent_waiting) {
                /* waitid returns 0; waitpid returns reaped pid. */
                g_coop_parent_resume_rax =
                    g_guest_waitid_active
                        ? 0ULL
                        : (uint64_t)(wr > 0 ? wr : (long)g_guest_fork_pid);
                g_coop_parent_resume_mode = 2;
            }
            bfree_coop_arm_parent_resume();
        } else {
            g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
            g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
            g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
            g_bfree_fork_parent_ret =
                g_guest_waitid_active
                    ? 0ULL
                    : (parent_waiting && wr > 0
                           ? (uint64_t)wr
                           : (uint64_t)(long)g_guest_fork_pid);
        }
        uart_puts("[VFORK] parent resume rip=");
        uart_puthex64(g_bfree_fork_saved_rcx);
        uart_puts(" rsp=");
        uart_puthex64(g_bfree_fork_saved_rsp);
        uart_puts(" rax=");
        uart_puthex64(g_bfree_fork_parent_ret);
        uart_puts(" wsp=");
        uart_puthex64((uint64_t)(unsigned long)g_guest_wait_status_ptr);
        uart_puts(" fs=");
        uart_puthex64(g_guest_fork_saved_fsbase);
        uart_puts(" pt=");
        uart_puthex64((uint64_t)(uintptr_t)resume_pt);
        uart_puts("\n");
    }
    /* Force parent AS: exit path / reap must not leave TCB on task_page_tables. */
    if (resume_pt && knl_current_task) {
        knl_current_task->page_table_base = resume_pt;
        __asm__ volatile("mov %0, %%cr3" :: "r"(resume_pt) : "memory");
    }
    g_bfree_sysret_exec_cr3 = 0;
    /*
     * Parent completed wait via FORK_PARENT: drop SIGCHLD so a stale CATCH
     * (bad sa_handler) cannot fire on the next normal syscall (printf).
     */
    g_guest_sig_pending &= ~(1ULL << 16); /* SIGCHLD = 17 */
    g_coop_parent_started = 0;
    g_coop_parent_in_wait = 0;
    g_guest_wait_status_ptr = 0;
    g_guest_waitid_active = 0;
    g_guest_waitid_infop = 0;
    return BFREE_SYSRET_FORK_PARENT;
}

static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;
    int blocking = (((unsigned)options & 1U) == 0U);

    for (;;) {
        /*
         * Ash `wait` uses waitpid(-1, WNOHANG) + sigsuspend. LIVE orphans
         * never deliver SIGCHLD → hang. Only heal on WNOHANG broad waits —
         * blocking waitpid(-1) must not kill in-flight pipeline children.
         */
        if (pid == -1 && !blocking) {
            if (bfree_process_live_count() > 0) {
                (void)bfree_process_force_zombie_live();
                g_guest_fork_active = 0;
                g_coop_parent_started = 0;
                g_guest_fork_was_as_copy = 0;
                g_coop_parent_in_wait = 0;
            }
        } else if (blocking && pid > 0) {
            if (bfree_process_force_zombie_except((int)pid) > 0) {
                if (bfree_process_child_pid() != (int)pid) {
                    g_guest_fork_active = 0;
                    g_coop_parent_started = 0;
                    g_guest_fork_was_as_copy = 0;
                }
            }
        }
        rc = bfree_process_wait4(pid, &status, (int)options);
        if (rc > 0) {
            g_guest_fork_status_ready = 0;
            g_coop_parent_in_wait = 0;
            g_guest_wait_status_ptr = 0;
            g_last_waitpid_status = status;
            if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
                *(int *)(uintptr_t)status_ptr = status;
            }
            /* Linux waitpid reaps one child per call — do not drain. */
            return rc;
        }
        if (rc < 0) {
            return rc; /* ECHILD */
        }
        if (!blocking) { /* WNOHANG */
            return 0;
        }
        /* Blocking wait-any: yield whenever a runnable child exists. */
        if (pid == -1) {
            if (bfree_process_runnable_count() > 0) {
                g_guest_fork_active = 1;
                g_guest_wait_status_ptr = status_ptr;
                g_guest_waitid_active = 0;
                g_coop_parent_in_wait = 1;
                if (bfree_process_first_live_pid() > 0) {
                    (void)bfree_process_select_pid(bfree_process_first_live_pid());
                }
                return bfree_coop_yield_to_child();
            }
            if (bfree_process_force_zombie_live() > 0) {
                g_guest_fork_active = 0;
                g_coop_parent_started = 0;
                g_guest_fork_was_as_copy = 0;
                continue;
            }
            {
                int er = bfree_guest_sig_take_eintr();
                if (er < 0) {
                    return er;
                }
            }
            return -10; /* ECHILD */
        }
        /* Specific-pid: yield only to the focused child matching pid. */
        if (pid > 0 && (g_guest_fork_was_as_copy || g_coop_parent_started ||
                        g_guest_fork_active)) {
            int focus = bfree_process_child_pid();
            if (focus == (int)pid && bfree_process_child_active() &&
                bfree_process_runnable_count() > 0) {
                g_guest_fork_active = 1;
                g_guest_wait_status_ptr = status_ptr;
                g_guest_waitid_active = 0;
                g_coop_parent_in_wait = 1;
                (void)bfree_process_select_pid((int)pid);
                return bfree_coop_yield_to_child();
            }
            /* Target live but not a healthy coop focus → force-reap it. */
            (void)bfree_process_force_zombie_live();
            g_guest_fork_active = 0;
            g_coop_parent_started = 0;
            g_guest_fork_was_as_copy = 0;
            continue;
        }
        if (bfree_process_force_zombie_live() > 0) {
            g_guest_fork_active = 0;
            g_coop_parent_started = 0;
            g_guest_fork_was_as_copy = 0;
            continue;
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        return -10; /* ECHILD */
    }
}

/* Minimal Linux waitid → wait4 bridge (siginfo filled for WEXITED). */
#define BFREE_P_ALL  0
#define BFREE_P_PID  1
#define BFREE_P_PGID 2
#define BFREE_WNOHANG_ID 0x00000001
#define BFREE_WEXITED    0x00000004
#define BFREE_SIGCHLD    17
#define BFREE_CLD_EXITED 1


static long sys_linux_waitid(long idtype, long id, long infop, long options)
{
    long pid;
    int status = 0;
    int wopts;
    long rc;
    int blocking;
    uint8_t *raw;

    if (idtype == BFREE_P_PID) {
        pid = id;
    } else if (idtype == BFREE_P_ALL) {
        pid = -1;
    } else {
        return -22; /* EINVAL: P_PGID not supported yet */
    }
    if ((options & BFREE_WEXITED) == 0 && (options & 0x00000002) == 0 &&
        (options & 0x00000008) == 0) {
        return -22;
    }
    wopts = ((options & BFREE_WNOHANG_ID) != 0) ? 1 : 0;
    blocking = (wopts == 0);

    for (;;) {
        /* Same heal/yield policy as waitpid — wait4 alone is non-blocking. */
        if (pid == -1 && !blocking) {
            if (bfree_process_live_count() > 0) {
                (void)bfree_process_force_zombie_live();
                g_guest_fork_active = 0;
                g_coop_parent_started = 0;
                g_guest_fork_was_as_copy = 0;
                g_coop_parent_in_wait = 0;
            }
        } else if (blocking && pid > 0) {
            if (bfree_process_force_zombie_except((int)pid) > 0) {
                if (bfree_process_child_pid() != (int)pid) {
                    g_guest_fork_active = 0;
                    g_coop_parent_started = 0;
                    g_guest_fork_was_as_copy = 0;
                }
            }
        }
        rc = bfree_process_wait4(pid, &status, wopts);
        if (rc > 0) {
            break;
        }
        if (rc < 0) {
            return rc;
        }
        if (!blocking) {
            return 0; /* WNOHANG, nothing ready */
        }
        if (pid == -1) {
            if (bfree_process_runnable_count() > 0) {
                g_guest_fork_active = 1;
                g_guest_wait_status_ptr = 0;
                g_guest_waitid_active = 1;
                g_guest_waitid_infop = infop;
                g_coop_parent_in_wait = 1;
                if (bfree_process_first_live_pid() > 0) {
                    (void)bfree_process_select_pid(bfree_process_first_live_pid());
                }
                return bfree_coop_yield_to_child();
            }
            if (bfree_process_force_zombie_live() > 0) {
                g_guest_fork_active = 0;
                g_coop_parent_started = 0;
                g_guest_fork_was_as_copy = 0;
                continue;
            }
        } else if (pid > 0 && bfree_process_runnable_count() > 0) {
            g_guest_fork_active = 1;
            g_guest_wait_status_ptr = 0;
            g_guest_waitid_active = 1;
            g_guest_waitid_infop = infop;
            g_coop_parent_in_wait = 1;
            (void)bfree_process_select_pid((int)pid);
            return bfree_coop_yield_to_child();
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }

    g_last_waitpid_status = status;
    g_guest_fork_status_ready = 0;
    g_guest_waitid_active = 0;
    g_coop_parent_in_wait = 0;
    if (infop != 0 && bfree_user_ptr_mapped(infop)) {
        raw = (uint8_t *)(uintptr_t)infop;
        {
            size_t i;
            for (i = 0; i < 32; ++i) {
                raw[i] = 0;
            }
        }
        *(int *)(void *)(raw + 0) = BFREE_SIGCHLD;
        *(int *)(void *)(raw + 4) = 0;
        *(int *)(void *)(raw + 8) = BFREE_CLD_EXITED;
        *(int *)(void *)(raw + 16) = (int)rc;
        *(unsigned int *)(void *)(raw + 20) = 0;
        *(int *)(void *)(raw + 24) = (status >> 8) & 0xff;
    }
    return 0;
}

/* exec_initrd path in kernel .bss — load_elf_image switches to kernel_page_table
 * and must not read a path string that still lives on the ring3 stack (would #GP). */
static char g_bfree_exec_initrd_kpath[64];

#ifdef BFREE_RUNTIME_BUILD
extern void timer_process_events(void);
#endif

extern int keyboard_has_data(void);
extern int keyboard_pop_char(uint32_t *out_char);
extern void mouse_poll_ps2(void);
extern int mouse_has_data(void);
extern int mouse_pop_state(int *x, int *y, int *buttons);

typedef struct {
    int type;
    uint32_t keycode;
    int mouse_x;
    int mouse_y;
    uint32_t mouse_btn;
} bfree_raw_input_event_t;

typedef struct {
    void *addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    int ready;
} bfree_framebuffer_info_t;

typedef struct {
    int used;
    int fd;
    int flags;
    int armed;
    int event_id;
    uint64_t next_expire_us;
    uint64_t interval_us;
    uint64_t expirations;
} bfree_timerfd_entry_t;

typedef struct {
    uint64_t bits[BFREE_SIGNAL_WORDS];
} bfree_kernel_sigset_t;

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} bfree_utsname_t;

typedef struct {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[32];
    unsigned int __ispeed;
    unsigned int __ospeed;
} bfree_termios_t;

#define BFREE_SC_PAGESIZE          30
#define BFREE_SC_PAGE_SIZE         BFREE_SC_PAGESIZE
#define BFREE_SC_NPROCESSORS_CONF  83
#define BFREE_SC_NPROCESSORS_ONLN  84
#define BFREE_SC_CLK_TCK           2
#define BFREE_SC_PHYS_PAGES        85
#define BFREE_SC_AVPHYS_PAGES      86
#define BFREE_SC_OPEN_MAX          4

#define BFREE_BRKINT 0x00000002U
#define BFREE_ICRNL  0x00000100U
#define BFREE_IXON   0x00000400U
#define BFREE_OPOST  0x00000001U
#define BFREE_ONLCR  0x00000004U
#define BFREE_CS8    0x00000030U
#define BFREE_CREAD  0x00000080U
#define BFREE_ISIG   0x00000001U
#define BFREE_ICANON 0x00000002U
#define BFREE_ECHO   0x00000008U
#define BFREE_TOSTOP 0x00000100U
#define BFREE_IEXTEN 0x00008000U

#define BFREE_VINTR  0
#define BFREE_VQUIT  1
#define BFREE_VERASE 2
#define BFREE_VEOF   4
#define BFREE_VMIN   6
#define BFREE_VTIME  5

static bfree_termios_t g_guest_tty_termios;
static int g_guest_tty_termios_inited;

static void bfree_guest_tty_defaults(bfree_termios_t *t)
{
    memset(t, 0, sizeof(*t));
    t->c_iflag = BFREE_BRKINT | BFREE_ICRNL | BFREE_IXON;
    t->c_oflag = BFREE_OPOST | BFREE_ONLCR;
    t->c_cflag = BFREE_CS8 | BFREE_CREAD;
    t->c_lflag = BFREE_ISIG | BFREE_ICANON | BFREE_ECHO | BFREE_IEXTEN;
    t->c_line = 0;
    t->__ispeed = 0;
    t->__ospeed = 0;
    t->c_cc[BFREE_VINTR] = 0x03;
    t->c_cc[BFREE_VQUIT] = 0x1c;
    t->c_cc[BFREE_VERASE] = 0x7f;
    t->c_cc[BFREE_VEOF] = 0x04;
    t->c_cc[BFREE_VMIN] = 1;
    t->c_cc[BFREE_VTIME] = 0;
}

static void bfree_guest_tty_ensure_init(void)
{
    if (!g_guest_tty_termios_inited) {
        bfree_guest_tty_defaults(&g_guest_tty_termios);
        g_guest_tty_termios_inited = 1;
    }
}

/* H07: controlling-tty job-control helpers (soft — no true process stop). */
static int bfree_guest_tty_self_pgid(void)
{
    if (g_guest_fork_active) {
        int pg = bfree_process_getpgid(g_guest_fork_pid);

        if (pg > 0) {
            return pg;
        }
    }
    return g_guest_pgid;
}

static int bfree_guest_tty_is_background(void)
{
    int self_pg;

    /* Interactive shell (!coop child): never soft-job-stop. H25 shell
     * setpgid(0) may move g_guest_pgid while tty_pgrp stays put; treating
     * that as bg silenced the prompt via soft SIGTTIN/TTOU. */
    if (!g_guest_fork_active) {
        return 0;
    }
    self_pg = bfree_guest_tty_self_pgid();
    return self_pg > 0 && g_guest_tty_pgrp > 0 && self_pg != g_guest_tty_pgrp;
}

/* Soft raise for bg tty access; returns EINTR when delivery paths fire. */
static long bfree_guest_tty_soft_job_sig(int sig)
{
    bfree_guest_sig_raise(sig);
    return bfree_guest_sig_take_eintr();
}

/* === H06 job-control / fg path === */
#ifndef BFREE_H06_JOBCTL_WIRED
#define BFREE_H06_JOBCTL_WIRED 1
#define BFREE_LINUX_TIOCGPGRP 0x540F
#define BFREE_LINUX_TIOCSPGRP 0x5410
#define BFREE_SIGTSTP 20
#define BFREE_SIGCONT 18
#define BFREE_SIGSTOP 19
#define BFREE_SIGTTIN 21
#define BFREE_SIGTTOU 22

static long sys_linux_setpgid(long pid, long pgid)
{
    int p = (int)pid;
    int g = (int)pgid;
    long rc;

    if (p < 0 || g < 0) {
        return -22;
    }
    /* pid==0 → calling process (shell or coop child focus). */
    if (p == 0) {
        if (g_guest_fork_active && g_coop_side == 1) {
            p = g_guest_fork_pid;
        } else {
            p = 1; /* init/shell */
        }
    }
    if (g == 0) {
        g = p;
    }
    if (p == 1 || (!g_guest_fork_active && p <= 1)) {
        g_guest_pgid = g;
        /* Shell moving its own pgid does not steal tty until tcsetpgrp. */
        return 0;
    }
    rc = bfree_process_setpgid(p, g);
    if (rc == 0 && g_guest_fork_active && p == g_guest_fork_pid) {
        g_guest_pgid = g;
    }
    return rc;
}

static long sys_linux_getpgid(long pid)
{
    int p = (int)pid;
    int pg;

    if (p < 0) {
        return -22;
    }
    if (p == 0) {
        return (long)bfree_guest_tty_self_pgid();
    }
    if (p == 1) {
        return (long)g_guest_pgid;
    }
    pg = bfree_process_getpgid(p);
    if (pg > 0) {
        return (long)pg;
    }
    return -3; /* ESRCH */
}

static long sys_linux_setsid(void)
{
    /* Single-session guest: become session leader of a new pgid. */
    int sid = g_guest_fork_active ? g_guest_fork_pid : 1;
    g_guest_sid = sid;
    g_guest_pgid = sid;
    /* New session is not yet foreground until tcsetpgrp. */
    return (long)g_guest_sid;
}

/* H06: stop coop child; parent resumes with WIFSTOPPED via wait. */
static long bfree_guest_stop_from_fork(int sig)
{
    int as_copy = g_guest_fork_was_as_copy;
    int pid = g_guest_fork_pid;
    uint64_t parent_fs = g_guest_fork_saved_fsbase;
    int stsig = sig & 0x7f;

    if (stsig == 0) {
        stsig = BFREE_SIGTSTP;
    }
    (void)bfree_process_stop_pid(pid, stsig);
    bfree_guest_sig_raise(17); /* SIGCHLD */
    bfree_coop_fd_switch_to(0);
    if (as_copy) {
        bfree_coop_as_switch_to(0);
        if (g_guest_parent_parked_heap_valid) {
            bfree_guest_load_heap_cwd(g_guest_parent_parked_cwd,
                                      g_guest_parent_parked_heap_next,
                                      g_guest_parent_parked_brk);
        }
    } else {
        bfree_guest_vfork_stack_restore();
        bfree_guest_load_heap_cwd(g_guest_fork_saved_cwd,
                                  g_guest_fork_saved_heap_next,
                                  g_guest_fork_saved_brk);
    }
    g_coop_side = 0;
    g_coop_child_blocked = 1;
    g_coop_parent_started = 1;
    g_guest_fork_active = 1;
    g_guest_fork_pid = pid;
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = parent_fs;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, parent_fs);
    g_bfree_sysret_exec_cr3 = 0;
    bfree_coop_publish_parent_resume();
    if (g_guest_wait_status_ptr != 0 &&
        bfree_user_ptr_mapped(g_guest_wait_status_ptr)) {
        *(int *)(uintptr_t)g_guest_wait_status_ptr = (stsig << 8) | 0x7f;
        g_guest_wait_status_ptr = 0;
    }
    if (g_bfree_fork_parent_ret == 0) {
        g_bfree_fork_parent_ret = (uint64_t)(long)pid;
    }
    return BFREE_SYSRET_FORK_PARENT;
}

#endif /* BFREE_H06_JOBCTL_WIRED */


static long bfree_guest_tty_set_termios(long termios_ptr)
{
    bfree_termios_t *src = (bfree_termios_t *)(uintptr_t)termios_ptr;

    if (src == 0 || !bfree_user_ptr_mapped(termios_ptr)) {
        return -14;
    }
    bfree_guest_tty_ensure_init();
    memcpy(&g_guest_tty_termios, src, sizeof(g_guest_tty_termios));
    return 0;
}

static bfree_timerfd_entry_t g_timerfd_entries[BFREE_MAX_TIMERFD];
static bfree_kernel_sigset_t g_signal_pending;
static bfree_kernel_sigset_t g_signal_mask;
#if defined(BFREE_WAYLAND_INPUT_STRICT) && BFREE_WAYLAND_INPUT_STRICT
static uint64_t g_input_deny_count;
static int g_input_deny_logged_once;
#endif
static int bfree_signal_any_ready(void);

static void bfree_copy_cstr(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t i;

    if (dst == 0 || src == 0 || dst_size == 0) {
        return;
    }

    for (i = 0; i + 1 < dst_size && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static uint32_t __attribute__((unused)) bfree_count_timerfd_entries(void)
{
    uint32_t count = 0;
    int index;

    for (index = 0; index < BFREE_MAX_TIMERFD; ++index) {
        if (g_timerfd_entries[index].used) {
            ++count;
        }
    }
    return count;
}

static uint8_t *bfree_user_stack_page_kptr(uint64_t vaddr);

static int bfree_user_vaddr_mapped(uint64_t vaddr)
{
    page_table_t *pt;

    if (!knl_current_task || !knl_current_task->page_table_base) {
        return 0;
    }
    if (vaddr >= VMM_USER_VA_BYTES) {
        return 0;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    /* Prefer VMM walk — direct pt->pt[] misses pt_ext / stale-PD cases. */
    if (vmm_user_page_mapped(pt, vaddr)) {
        return 1;
    }
    return bfree_user_stack_page_kptr(vaddr) != 0;
}

static int guest_serial_boot_logs;

int bfree_guest_serial_reset_boot_logs(void)
{
    guest_serial_boot_logs = 0;
    return 0;
}

long sys_debug_serial_write(long str_ptr, long max_len)
{
    const char *p;
    uint32_t i;
    uint32_t limit;

    if (str_ptr == 0 || max_len <= 0) {
        return -1;
    }

    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)str_ptr)) {
        if (guest_serial_boot_logs < 12) {
            uart_puts("[guest-serial] reject unmapped ptr=");
            uart_puthex64((uint64_t)(uintptr_t)str_ptr);
            uart_puts(" len=");
            uart_puthex64((uint64_t)max_len);
            uart_puts("\n");
            ++guest_serial_boot_logs;
        }
        return -1;
    }

    /* Same as copy_user_cstr: handler runs on task CR3, so user VAs are valid. */
    p = (const char *)(uintptr_t)str_ptr;

    limit = (uint32_t)max_len;
    /* Keep the previous upper bound to avoid huge output storms. */
    if (limit > 159U) {
        limit = 159U;
    }

    if (guest_serial_boot_logs < 12) {
        uart_puts("[guest-serial] ptr=");
        uart_puthex64((uint64_t)(uintptr_t)str_ptr);
        uart_puts(" len=");
        uart_puthex64((uint64_t)limit);
        uart_puts("\n");
        ++guest_serial_boot_logs;
    }

    /* syscall_entry.S switches to bfree_syscall_kstack before calling here. */
    for (i = 0; i < limit; ++i) {
        char ch;
        uint64_t addr = (uint64_t)(uintptr_t)str_ptr + (uint64_t)i;

        if ((i & 0xFFFU) == 0U && !bfree_user_vaddr_mapped(addr)) {
            break;
        }
        ch = p[i];
        if (ch == '\0') {
            break;
        }
        if (ch == '\n') {
            uart_putc('\r');
        }
        uart_putc(ch);
    }
    return (long)i;
}

// ---------------------------------------------------------------
// Wayland IPC / メモリ / 時刻 syscall (25-29)
// ---------------------------------------------------------------

// case 25: sys_pipe — same backing as Linux pipe2(flags=0)
long sys_pipe(long pipefd_ptr)
{
    return sys_linux_pipe2(pipefd_ptr, 0);
}

#define BFREE_GUEST_MEMFD_FD        0x3718

// case 26: sys_mmap
// wl_shm バッファ共有に必要。/dev/fb0 はユーザ CR3 へ物理 VRAM を固定 VA にマップする。
// MAP_ANONYMOUS は BFREE_GUEST_HEAP_* に PMM ページを割り当て（musl malloc/Qt 用）。
static int bfree_map_guest_heap_range(page_table_t *pt, uint64_t lo, uint64_t hi)
{
    uint64_t v;

    if (!pt) {
        return -1;
    }
    lo &= ~(PAGE_SIZE - 1ULL);
    hi = (hi + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    if (lo < (uint64_t)BFREE_GUEST_HEAP_BASE || hi > (uint64_t)BFREE_GUEST_HEAP_LIMIT) {
        return -1;
    }
    if (hi > VMM_USER_VA_BYTES) {
        return -1;
    }

    bfree_kernel_phys_io_begin();
    for (v = lo; v < hi; v += PAGE_SIZE) {
        void *page;

        if (vmm_user_page_mapped(pt, v)) {
            continue;
        }
        /* Clone leaves supervisor identity PTEs (0x003, VA==PA); clear before user anon map. */
        (void)vmm_unmap_page(pt, v);
        page = pmm_alloc();
        if (!page) {
            bfree_kernel_phys_io_end();
            return -12;
        }
        if (bfree_kernel_clear_phys((uint64_t)(uintptr_t)page, PAGE_SIZE) != 0) {
            bfree_kernel_phys_io_end();
            return -1;
        }
        if (vmm_map_page(pt, v, (uint64_t)(uintptr_t)page, 0x007ULL) != 0) {
            bfree_kernel_phys_io_end();
            return -1;
        }
        vmm_drop_identity_alias(pt, (uint64_t)(uintptr_t)page);
        vmm_drop_identity_alias(&kernel_page_table, (uint64_t)(uintptr_t)page);
    }
    bfree_kernel_phys_io_end();
    return 0;
}

/* Guest desktop: ctor stack @0x08000000, fallback @0x19000000 (see guest_link_compat.cpp). */
#define BFREE_GUEST_RESERVE_LO   0x08000000ULL
#define BFREE_GUEST_RESERVE_MID  0x18000000ULL
#define BFREE_GUEST_RESERVE_HI   0x19000000ULL

static int bfree_guest_skip_reserved_mmap(uint64_t *vaddr, uint64_t want_bytes)
{
    uint64_t v = *vaddr;

    if (v < BFREE_GUEST_RESERVE_LO && v + want_bytes > BFREE_GUEST_RESERVE_LO)
        v = BFREE_GUEST_RESERVE_MID;
    if (v < BFREE_GUEST_RESERVE_HI && v + want_bytes > BFREE_GUEST_RESERVE_HI)
        return -12;
    *vaddr = v;
    return 0;
}

static void bfree_guest_heap_next_commit(uint64_t vaddr, uint64_t want_bytes, int is_fixed)
{
    uint64_t hi = vaddr + want_bytes;

    if (is_fixed) {
        /* Meta mmap below ctor stack must advance cursor; reserved arenas must not. */
        if (vaddr < BFREE_GUEST_RESERVE_LO && g_guest_heap_next < hi)
            g_guest_heap_next = hi;
    } else if (g_guest_heap_next < hi) {
        g_guest_heap_next = hi;
    }
}

static void bfree_guest_heap_unmap_range(page_table_t *pt, uint64_t lo, uint64_t hi)
{
    uint64_t v;

    if (!pt) {
        return;
    }
    lo &= ~(PAGE_SIZE - 1ULL);
    hi = (hi + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    if (hi <= lo) {
        return;
    }
    bfree_kernel_phys_io_begin();
    for (v = lo; v < hi; v += PAGE_SIZE) {
        (void)vmm_unmap_page(pt, v);
    }
    bfree_kernel_phys_io_end();
    /* Flush stale TLB entries for the freed range (see sys_munmap). */
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
}

static long bfree_guest_mmap_vfile(long addr, long length, long flags, long fd, long offset);
static void bfree_guest_shared_mmap_flush_range(uint64_t lo, uint64_t hi);
static long sys_mmap_anonymous_heap(long addr, long length, long flags)
{
    uint64_t want_bytes;
    uint64_t vaddr;
    page_table_t *pt;

    if (length <= 0) {
        return -22;
    }
    want_bytes = (uint64_t)(unsigned long)length;
    want_bytes = (want_bytes + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    if (want_bytes == 0ULL) {
        return -22;
    }

    if (knl_current_task == 0 || knl_current_task->page_table_base == 0) {
        return -1;
    }

    if (flags & MAP_FIXED) {
        if (addr == 0) {
            /* Qt/musl may pass MAP_FIXED|MAP_ANONYMOUS at NULL; treat as kernel-chosen VA. */
            flags &= ~MAP_FIXED;
        } else {
            vaddr = (uint64_t)(unsigned long)addr;
            vaddr &= ~(PAGE_SIZE - 1ULL);
        }
    }
    if (!(flags & MAP_FIXED)) {
        vaddr = g_guest_heap_next;
        if (vaddr < g_guest_brk)
            vaddr = g_guest_brk;
        vaddr = (vaddr + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
        if (bfree_guest_skip_reserved_mmap(&vaddr, want_bytes) != 0)
            return -12;
    }

    if (vaddr < (uint64_t)BFREE_GUEST_HEAP_BASE ||
        vaddr + want_bytes > (uint64_t)BFREE_GUEST_HEAP_LIMIT ||
        vaddr + want_bytes > VMM_USER_VA_BYTES) {
        return -12;
    }

    pt = (page_table_t *)knl_current_task->page_table_base;
    if (bfree_map_guest_heap_range(pt, vaddr, vaddr + want_bytes) != 0) {
        return -12;
    }

    /* POSIX: MAP_ANONYMOUS memory must be zero-filled. map_guest_heap_range
     * skips pages that are already mapped (e.g. reused after an exec-time
     * heap reset), which leaves stale data. musl mallocng's calloc skips
     * memset for "fresh" mmap memory, so regcomp (sed/grep) then reads
     * garbage as pointers and traps in get_meta. Keep MAP_FIXED untouched:
     * Qt reserve arenas re-fix over live data. */
    if (!(flags & MAP_FIXED)) {
        memset((void *)(uintptr_t)vaddr, 0, (size_t)want_bytes);
    } else if (vaddr >= (uint64_t)BFREE_GUEST_HEAP_BASE &&
               vaddr + want_bytes <= (uint64_t)BFREE_GUEST_RESERVE_LO) {
        /* musl mallocng MAP_FIXED|ANON over the brk/meta band expects
         * zeroed pages; exec-time premapping leaves them mapped. */
        memset((void *)(uintptr_t)vaddr, 0, (size_t)want_bytes);
    }

    bfree_guest_heap_next_commit(vaddr, want_bytes, (flags & MAP_FIXED) ? 1 : 0);

    return (long)(uintptr_t)vaddr;
}

static void sys_mmap_fb_diag(long code, uint64_t a, uint64_t b)
{
    static unsigned g_sys_mmap_fb_diag_count;

    if (g_sys_mmap_fb_diag_count >= 6U) {
        return;
    }
    ++g_sys_mmap_fb_diag_count;
    uart_puts("[MMAP] fb0 fail code=");
    uart_puthex64((uint64_t)code);
    uart_puts(" a=");
    uart_puthex64(a);
    uart_puts(" b=");
    uart_puthex64(b);
    uart_puts("\n");
}

long sys_mmap(long addr, long length, long prot, long flags, long fd)
{
    tk2gpu_fbinfo_t fbinfo;
    uint64_t phys;
    uint64_t line_bytes;
    uint64_t cap_bytes;
    uint64_t want_bytes;
    page_table_t *pt;
    size_t off;
    long mmap_off;

    (void)prot;
    /* Linux mmap arg6 is byte offset (page-aligned). Captured from R9 in entry. */
    mmap_off = (long)g_bfree_user_syscall_r9;
    if (fd == BFREE_FB0_FD) {
        /* framebuffer path below */
    } else if (fd == (long)BFREE_GUEST_MEMFD_FD) {
        return sys_mmap_anonymous_heap(addr, length, flags | MAP_ANONYMOUS);
    } else if (fd < 0 || (flags & MAP_ANONYMOUS)) {
        return sys_mmap_anonymous_heap(addr, length, flags);
    } else {
        return bfree_guest_mmap_vfile(addr, length, flags, fd, mmap_off);
    }

    (void)addr;

    if (runtime_fbdev_ioctl(TK2GPU_IOCTL_GET_INFO, &fbinfo) != 0) {
        sys_mmap_fb_diag(-101, (uint64_t)fd, (uint64_t)flags);
        return -1;
    }

    phys = fbinfo.phys_addr;
    if (phys == 0ULL) {
        sys_mmap_fb_diag(-102, fbinfo.pitch, fbinfo.height);
        return -1;
    }
    if ((phys & (PAGE_SIZE - 1ULL)) != 0ULL) {
        sys_mmap_fb_diag(-103, phys, 0ULL);
        return -22;
    }

    line_bytes = (uint64_t)fbinfo.pitch * (uint64_t)fbinfo.height;
    cap_bytes = (uint64_t)fbinfo.size;
    if (line_bytes > 0ULL && line_bytes < cap_bytes) {
        cap_bytes = line_bytes;
    }
    if (cap_bytes == 0ULL) {
        sys_mmap_fb_diag(-104, fbinfo.pitch, fbinfo.height);
        return -1;
    }

    if (length <= 0) {
        want_bytes = cap_bytes;
    } else {
        want_bytes = (uint64_t)(unsigned long)length;
        if (want_bytes > cap_bytes) {
            want_bytes = cap_bytes;
        }
    }

    if (want_bytes > (uint64_t)BFREE_FB0_USER_MMAP_MAX_SIZE) {
        sys_mmap_fb_diag(-105, want_bytes, (uint64_t)BFREE_FB0_USER_MMAP_MAX_SIZE);
        return -12;
    }

    want_bytes = (want_bytes + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    if (want_bytes > (uint64_t)BFREE_FB0_USER_MMAP_MAX_SIZE) {
        sys_mmap_fb_diag(-106, want_bytes, (uint64_t)BFREE_FB0_USER_MMAP_MAX_SIZE);
        return -12;
    }

    if (knl_current_task == 0 || knl_current_task->page_table_base == 0) {
        sys_mmap_fb_diag(-107, 0ULL, 0ULL);
        return -1;
    }

    pt = (page_table_t *)knl_current_task->page_table_base;
    for (off = 0; off < (size_t)want_bytes; off += (size_t)PAGE_SIZE) {
        uint64_t vaddr = (uint64_t)BFREE_FB0_USER_MMAP_BASE + (uint64_t)off;
        /* Drop cloned identity supervisor PTE before installing user VRAM mapping. */
        (void)vmm_unmap_page(pt, vaddr);
        if (vmm_map_page(pt, vaddr, phys + (uint64_t)off, 0x007ULL) != 0) {
            sys_mmap_fb_diag(-108, vaddr, phys + (uint64_t)off);
            return -1;
        }
    }

    return (long)(uintptr_t)BFREE_FB0_USER_MMAP_BASE;
}

// case 27: sys_shm_open — implemented later via vfile (shm/<name>)
static long bfree_guest_shm_open(long name_ptr, long oflag, long mode);
static long bfree_guest_shm_unlink(long name_ptr);

long sys_shm_open(long name_ptr, long oflag, long mode)
{
    return bfree_guest_shm_open(name_ptr, oflag, mode);
}

// case 28: sys_shm_unlink
long sys_shm_unlink(long name_ptr)
{
    return bfree_guest_shm_unlink(name_ptr);
}

/* Soft CLOCK_REALTIME override from clock_settime / settimeofday. */
static int g_guest_realtime_override;
static long g_guest_realtime_sec;
static long g_guest_realtime_nsec;

// case 29 / Linux 228: sys_clock_gettime
// CLOCK_MONOTONIC / CLOCK_REALTIME → uptime_us ベースで返す
long sys_clock_gettime(long clockid, long timespec_ptr)
{
    struct timespec *ts = (struct timespec *)timespec_ptr;
    uint64_t us;

    if (ts == 0) {
        return -22;
    }
    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ts)) {
        return -14;
    }
    /* CLOCK_REALTIME(0): honor soft override when set. */
    if (clockid == 0 && g_guest_realtime_override) {
        ts->tv_sec = g_guest_realtime_sec;
        ts->tv_nsec = g_guest_realtime_nsec;
        return 0;
    }
    us = knl_get_current_time(); // uptime_us
    ts->tv_sec  = (long)(us / 1000000ULL);
    ts->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    return 0;
}

// Linux 96: gettimeofday — musl/Qt use the Linux nr, not B-Free clock_gettime(29).
long sys_gettimeofday(long tv_ptr, long tz_ptr)
{
    struct timeval *tv = (struct timeval *)tv_ptr;
    uint64_t us;

    (void)tz_ptr;
    if (tv == 0) {
        return 0;
    }
    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)tv)) {
        return -14;
    }
    us = knl_get_current_time();
    tv->tv_sec  = (long)(us / 1000000ULL);
    tv->tv_usec = (long)(us % 1000000ULL);
    return 0;
}

#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif
#define BFREE_ARCH_SET_FS 0x1002L
#define BFREE_ARCH_GET_FS 0x1003L

static void bfree_wrmsr64(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static uint64_t bfree_rdmsr64(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static void bfree_restore_user_fsbase(void) __attribute__((unused));
static void bfree_restore_user_fsbase(void)
{
    uint64_t fsbase = 0;

    if (knl_current_task != 0) {
        fsbase = knl_current_task->user_fsbase;
    }
    if (fsbase == 0) {
        return;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, fsbase);
}

#define BFREE_EARLY_TCB_BYTES 256ULL

/*
 * musl __pthread_self reads the self pointer from %fs:0 before __init_tls.
 * If that word is zero, errno/TLS helpers dereference NULL+0x28 → CR2=0x28.
 * Match tools/guest_link_compat.cpp: zeroed early TCB below RSP + self word.
 */
static int bfree_user_exec_bootstrap_early_tls(uint64_t user_rsp, uint64_t *out_fsbase)
{
    uint64_t tcb;
    uint64_t off;
    uint64_t probe;
    uint64_t self_word;
    uint8_t zbuf[64];

    if (!out_fsbase || user_rsp < BFREE_EARLY_TCB_BYTES + PAGE_SIZE) {
        return -1;
    }
    tcb = (user_rsp - BFREE_EARLY_TCB_BYTES) & ~0xFULL;
    if (bfree_user_stack_page_phys(tcb, &probe) != 0) {
        return -1;
    }
    for (off = 0; off < BFREE_EARLY_TCB_BYTES; off += sizeof(zbuf)) {
        uint64_t chunk = BFREE_EARLY_TCB_BYTES - off;
        if (chunk > sizeof(zbuf)) {
            chunk = sizeof(zbuf);
        }
        memset(zbuf, 0, sizeof(zbuf));
        if (bfree_user_stack_poke_bytes(tcb + off, (const char *)zbuf, chunk) != 0) {
            return -1;
        }
    }
    self_word = tcb;
    if (bfree_user_stack_poke_bytes(tcb, (const char *)&self_word, sizeof(self_word)) != 0) {
        return -1;
    }
    *out_fsbase = tcb;
    return 0;
}

static void bfree_user_exec_install_fsbase(uint64_t user_rsp, int bootstrap_tls)
{
    uint64_t early_fs = 0;

    if (bootstrap_tls && bfree_user_exec_bootstrap_early_tls(user_rsp, &early_fs) == 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = early_fs;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, early_fs);
        uart_puts("[TLS] exec early fsbase=");
        uart_puthex64(early_fs);
        uart_puts("\n");
        return;
    }
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);
    if (bootstrap_tls) {
        uart_puts("[TLS] exec early bootstrap failed rsp=");
        uart_puthex64(user_rsp);
        uart_puts("\n");
    }
}

// Linux 158: arch_prctl — musl __init_tls sets %fs via ARCH_SET_FS.
long sys_arch_prctl(long code, long addr)
{
    if (code == BFREE_ARCH_SET_FS) {
        uint64_t fsbase = (uint64_t)(uintptr_t)addr;
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, fsbase);
        return 0;
    }
    if (code == BFREE_ARCH_GET_FS) {
        uint64_t fsbase = bfree_rdmsr64((uint32_t)BFREE_MSR_FS_BASE);
        uint64_t *out = (uint64_t *)(uintptr_t)addr;
        if (out == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)out)) {
            return -14;
        }
        *out = fsbase;
        return 0;
    }
    return -22;
}

// Linux 11: munmap — must actually drop guest heap PTEs. A no-op stub that
// always returns success leaves musl mallocng metadata pointing at freed VA
// (observed: regcomp/sed GPF in get_meta after munmap length ~0x13ff000).
long sys_munmap(long addr, long length)
{
    uint64_t lo;
    uint64_t hi;
    uint64_t len;
    uint64_t v;
    page_table_t *pt;

    if (length <= 0) {
        return -22;
    }
    len = (uint64_t)(unsigned long)length;
    if (knl_current_task == 0 || knl_current_task->page_table_base == 0) {
        return -1;
    }
    /* Corrupt meta often passes stack-ish garbage (~20 MiB); refuse so musl
     * keeps the mapping and does not drop the group from its book-keeping. */
    if (len > 32ULL * 1024ULL * 1024ULL) {
        return -22;
    }
    lo = (uint64_t)(uintptr_t)addr & ~(PAGE_SIZE - 1ULL);
    hi = lo + len;
    hi = (hi + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    if (hi <= lo || hi > VMM_USER_VA_BYTES) {
        return -22;
    }
    if (lo < (uint64_t)BFREE_GUEST_HEAP_BASE ||
        hi > (uint64_t)BFREE_GUEST_HEAP_LIMIT) {
        return 0;
    }

    /* A5: flush MAP_SHARED vfile mappings before pages go away. */
    bfree_guest_shared_mmap_flush_range(lo, hi);

    pt = (page_table_t *)knl_current_task->page_table_base;
    bfree_kernel_phys_io_begin();
    for (v = lo; v < hi; v += PAGE_SIZE) {
        (void)vmm_unmap_page(pt, v);
    }
    bfree_kernel_phys_io_end();
    /* vmm_unmap_page only clears the PTE; without a TLB flush the CPU keeps
     * translating the old VA→PA and a later mmap at the same VA silently
     * aliases the stale page (mallocng heap corruption). */
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
    return 0;
}

// Linux 10: mprotect — anonymous guest mappings are already R/W.
long sys_mprotect(long addr, long length, long prot)
{
    (void)addr;
    (void)length;
    (void)prot;
    return 0;
}

// Linux 28: madvise — QV4 PageReservation::decommit guard pages; noop on guest arena.
static long sys_linux_madvise(long addr, long length, long advice)
{
    /* Known Linux MADV_* (0..12, 100..101 soft-ok). Reject junk. */
    if (length < 0) {
        return -22;
    }
    if (advice < 0 || (advice > 12 && advice < 100) || advice > 101) {
        return -22; /* EINVAL */
    }
    (void)addr;
    return 0;
}

/* Linux 27: mincore — report pages present (guest anon/vfile always "in core"). */
static long sys_linux_mincore(long addr, long length, long vec)
{
    size_t pages;
    size_t i;
    uint8_t *out;

    if (length < 0) {
        return -22;
    }
    if (vec == 0 || !bfree_user_ptr_mapped(vec)) {
        return -14;
    }
    if (addr != 0 && !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)addr)) {
        return -12; /* ENOMEM for unmapped start — soft */
    }
    pages = ((size_t)length + 4095U) / 4096U;
    if (pages == 0) {
        pages = 1;
    }
    out = (uint8_t *)(uintptr_t)vec;
    for (i = 0; i < pages; ++i) {
        out[i] = 1;
    }
    return 0;
}

/* Linux 25: mremap — grow/shrink; MAYMOVE copies to a fresh anon mapping. */
static long sys_linux_mremap(long old_addr, long old_size, long new_size, long flags,
                             long new_addr)
{
    (void)new_addr;
    if (old_size <= 0 || new_size <= 0) {
        return -22;
    }
    if (new_size == old_size) {
        return old_addr;
    }
    if ((flags & 1L) != 0) { /* MREMAP_MAYMOVE */
        long mapped;
        size_t ncopy;
        size_t i;
        uint8_t *src;
        uint8_t *dst;

        mapped = sys_mmap_anonymous_heap(0, new_size, MAP_ANONYMOUS | MAP_PRIVATE);
        if (mapped < 0) {
            return mapped;
        }
        ncopy = (size_t)((new_size < old_size) ? new_size : old_size);
        src = (uint8_t *)(uintptr_t)(uint64_t)old_addr;
        dst = (uint8_t *)(uintptr_t)(uint64_t)mapped;
        for (i = 0; i < ncopy; ++i) {
            dst[i] = src[i];
        }
        (void)sys_munmap(old_addr, old_size);
        return mapped;
    }
    if (new_size < old_size) {
        (void)sys_munmap(old_addr + new_size, old_size - new_size);
        return old_addr;
    }
    /* Expand in place only when the extension is free — else ENOMEM. */
    {
        long ext = sys_mmap_anonymous_heap(old_addr + old_size, new_size - old_size,
                                           MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE);
        if (ext < 0) {
            return -12; /* ENOMEM */
        }
        return old_addr;
    }
}

/* Linux 324 / 334: empty-success stubs for musl/pthread bootstrap. */
static long sys_linux_membarrier(long cmd, long flags, long cpu_id)
{
    (void)cmd;
    (void)flags;
    (void)cpu_id;
    return 0;
}

static long sys_linux_rseq(long rseq_ptr, long rseq_len, long flags, long sig)
{
    (void)rseq_ptr;
    (void)rseq_len;
    (void)flags;
    (void)sig;
    return 0;
}

// Linux 12: brk — map new pages when musl extends the break (was pointer-only).
long sys_brk(long addr)
{
    uint64_t new_brk;
    page_table_t *pt;

    if (addr == 0) {
        return (long)(uintptr_t)g_guest_brk;
    }
    if (knl_current_task == 0 || knl_current_task->page_table_base == 0) {
        return (long)(uintptr_t)g_guest_brk;
    }

    new_brk = (uint64_t)(uintptr_t)addr;
    if (new_brk < (uint64_t)BFREE_GUEST_HEAP_BASE ||
        new_brk > (uint64_t)BFREE_GUEST_HEAP_LIMIT ||
        new_brk > VMM_USER_VA_BYTES) {
        return (long)(uintptr_t)g_guest_brk;
    }
    if (new_brk == g_guest_brk) {
        return addr;
    }
    if (new_brk < g_guest_brk) {
        g_guest_brk = new_brk;
        return addr;
    }

    /* musl mallocng interleaves brk (meta areas) and mmap (groups). Once
     * mmap has handed out anything above the current break, growing the
     * break would overlap live mmap memory and corrupt the guest heap
     * (observed: sed/regcomp dying in mallocng get_meta). Refuse; musl
     * falls back to mmap for meta areas. */
    if (g_guest_heap_next > g_guest_brk) {
        return (long)(uintptr_t)g_guest_brk;
    }

    pt = (page_table_t *)knl_current_task->page_table_base;
    if (bfree_map_guest_heap_range(pt, g_guest_brk, new_brk) != 0) {
        return (long)(uintptr_t)g_guest_brk;
    }
    /* Linux brk memory is zero-filled. map_guest_heap_range keeps pages that
     * were premapped by the exec-time heap reset, so they still hold the
     * previous process's data; mallocng stores meta areas here and assumes
     * zeroed memory. */
    memset((void *)(uintptr_t)g_guest_brk, 0, (size_t)(new_brk - g_guest_brk));
    g_guest_brk = new_brk;
    if (g_guest_heap_next < new_brk) {
        g_guest_heap_next = new_brk;
    }
    return addr;
}

/* Linux 274: get_robust_list — musl pthread init; single-threaded guest has none.
 * (Historically miswired as case 200 / tkill.) */
static long sys_linux_get_robust_list(long pid, long head_ptr, long len_ptr)
{
    (void)pid;
    if (head_ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)head_ptr)) {
        *(uintptr_t *)(uintptr_t)head_ptr = 0;
    }
    if (len_ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)len_ptr)) {
        *(size_t *)(uintptr_t)len_ptr = sizeof(uintptr_t);
    }
    return 0;
}

/* Linux 273: set_robust_list */
static long sys_linux_set_robust_list_real(long head, long len)
{
    (void)head;
    (void)len;
    return 0;
}

// Linux 218: set_tid_address — remember cleartid; do not poke TLS memory on set.
long sys_set_tid_address(long tid_ptr)
{
    g_guest_clear_child_tid = (uintptr_t)tid_ptr;
    if (g_guest_thread_active) {
        return (long)g_guest_thread_tid;
    }
    if (g_guest_fork_active) {
        return (long)g_guest_fork_pid;
    }
    return 1;
}

static unsigned g_guest_futex_log_count;
static uint64_t bfree_timespec_to_us(const struct timespec *ts);

/*
 * Linux 202: futex — guest coop (H20).
 * - Timed WAIT: poll until value changes or deadline; leave *uaddr locked on
 *   ETIMEDOUT.
 * - Untimed WAIT: brief coop yield, arm waiter slot, short spin, then clear
 *   *uaddr (Qt single-thread workaround; full preemptive park → later).
 * - WAKE: disarm matching waiter slots (soft-1 if none).
 */
#define BFREE_FUTEX_WAITERS 8
static volatile int *g_futex_waiter_uaddr[BFREE_FUTEX_WAITERS];
static int g_futex_waiter_armed[BFREE_FUTEX_WAITERS];

static void bfree_guest_futex_wake_user(volatile int *uaddr)
{
    int i;

    if (!uaddr) {
        return;
    }
    preempt_disable();
    for (i = 0; i < BFREE_FUTEX_WAITERS; ++i) {
        if (g_futex_waiter_armed[i] && g_futex_waiter_uaddr[i] == uaddr) {
            g_futex_waiter_armed[i] = 0;
            g_futex_waiter_uaddr[i] = 0;
        }
    }
    preempt_enable();
}

/* Coop: when another guest thread is runnable, switch (Linux-like yield). */
static long bfree_guest_sched_maybe_yield(void)
{
    int other;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    if (!g_guest_need_resched) {
        return 0;
    }
    g_guest_need_resched = 0;
    other = bfree_gthr_find_runnable(g_gthr_cur);
    if (other < 0) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].rax = 0;
    g_gthr[g_gthr_cur].state = BFREE_GTHR_RUNNABLE;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static int bfree_futex_arm_waiter(volatile int *ua)
{
    int i;

    preempt_disable();
    for (i = 0; i < BFREE_FUTEX_WAITERS; ++i) {
        if (!g_futex_waiter_armed[i]) {
            g_futex_waiter_uaddr[i] = ua;
            g_futex_waiter_armed[i] = 1;
            preempt_enable();
            return i;
        }
    }
    preempt_enable();
    return -1;
}

static void bfree_futex_disarm_slot(int slot)
{
    if (slot < 0 || slot >= BFREE_FUTEX_WAITERS) {
        return;
    }
    preempt_disable();
    g_futex_waiter_armed[slot] = 0;
    g_futex_waiter_uaddr[slot] = 0;
    preempt_enable();
}

static int bfree_futex_slot_woken(int slot, volatile int *ua, int val)
{
    if (slot < 0) {
        return *(volatile int *)ua != val;
    }
    return !g_futex_waiter_armed[slot] || *(volatile int *)ua != val;
}

long sys_futex(long uaddr, long op, long val, long timeout_ptr, long uaddr2, long val3)
{
    int cmd = (int)(op & 0x7f);

    (void)uaddr2;
    (void)val3;

    switch (cmd) {
    case 0: /* FUTEX_WAIT */
    case 9: /* FUTEX_WAIT_BITSET */
        if (uaddr == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)uaddr)) {
            return -14; /* EFAULT */
        }
        if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
            return 0;
        }
        if (timeout_ptr != 0) {
            struct timespec *ts = (struct timespec *)(uintptr_t)timeout_ptr;
            uint64_t wait_us;
            uint64_t start;

            if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ts)) {
                return -14;
            }
            wait_us = bfree_timespec_to_us(ts);
            start = knl_get_current_time();
            __asm__ volatile("sti" ::: "memory");
            while ((knl_get_current_time() - start) < wait_us) {
                if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
                    __asm__ volatile("cli" ::: "memory");
                    if (g_guest_futex_log_count < 8U) {
                        ++g_guest_futex_log_count;
                        uart_puts("[FUTEX] wait woken\n");
                    }
                    return 0;
                }
                __asm__ volatile("pause" ::: "memory");
            }
            __asm__ volatile("cli" ::: "memory");
            if (g_guest_futex_log_count < 8U) {
                ++g_guest_futex_log_count;
                uart_puts("[FUTEX] wait ETIMEDOUT\n");
            }
            return -110; /* ETIMEDOUT — leave word locked */
        }
        {
            long yr = bfree_guest_sched_maybe_yield();
            if (yr != 0) {
                return yr;
            }
        }
        if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
            return 0;
        }
        {
            long sw = bfree_gthr_park_futex((volatile int *)(uintptr_t)uaddr, (int)val);

            if (sw != 0) {
                return sw;
            }
        }
        {
            int slot = bfree_futex_arm_waiter((volatile int *)(uintptr_t)uaddr);
            int spins;
            __asm__ volatile("sti" ::: "memory");
            for (spins = 0; spins < 64; ++spins) {
                if (bfree_futex_slot_woken(slot, (volatile int *)(uintptr_t)uaddr,
                                           (int)val)) {
                    break;
                }
                {
                    long yr = bfree_guest_sched_maybe_yield();
                    if (yr != 0) {
                        __asm__ volatile("cli" ::: "memory");
                        bfree_futex_disarm_slot(slot);
                        return yr;
                    }
                }
                __asm__ volatile("pause" ::: "memory");
            }
            __asm__ volatile("cli" ::: "memory");
            bfree_futex_disarm_slot(slot);
        }
        /* Compat2: never clear *uaddr on untimed WAIT (generic musl/pthread).
         * Single-thread: spin/yield already done; return 0 if value changed,
         * else EAGAIN so callers can retry (no silent unlock). */
        if (!bfree_gthr_mt()) {
            if (*(volatile int *)(uintptr_t)uaddr != (int)val) {
                if (g_guest_futex_log_count < 8U) {
                    ++g_guest_futex_log_count;
                    uart_puts("[FUTEX] wait\n");
                }
                return 0;
            }
            if (g_guest_futex_log_count < 8U) {
                ++g_guest_futex_log_count;
                uart_puts("[FUTEX] wait EAGAIN\n");
            }
            return -11; /* EAGAIN — still locked */
        }
        if (g_guest_futex_log_count < 8U) {
            ++g_guest_futex_log_count;
            uart_puts("[FUTEX] wait\n");
        }
        return 0;
    case 1: /* FUTEX_WAKE */
        if ((int)val <= 0) {
            return 0;
        }
        {
            int want = (int)val;
            int woke = 0;
            int i;
            long sw;

            preempt_disable();
            for (i = 0; i < BFREE_FUTEX_WAITERS && woke < want; ++i) {
                if (g_futex_waiter_armed[i] &&
                    g_futex_waiter_uaddr[i] == (volatile int *)(uintptr_t)uaddr) {
                    g_futex_waiter_armed[i] = 0;
                    g_futex_waiter_uaddr[i] = 0;
                    ++woke;
                }
            }
            preempt_enable();
            sw = bfree_gthr_on_futex_wake((volatile int *)(uintptr_t)uaddr, want);
            if (sw != 0) {
                return sw;
            }
            return woke > 0 ? (long)woke : 1; /* soft success if no waiter */
        }
    case 3: /* FUTEX_REQUEUE */
    case 4: /* FUTEX_CMP_REQUEUE */
    case 5: /* FUTEX_WAKE_OP */
    case 10: /* FUTEX_WAKE_BITSET */
        return 0;
    default:
        return -22; /* EINVAL */
    }
}

// Linux 97: getrlimit — align with prlimit64 resource cases.
static uint64_t g_rlim_stack_cur = 8ULL * 1024ULL * 1024ULL;
static uint64_t g_rlim_stack_max = 16ULL * 1024ULL * 1024ULL;
static uint64_t g_rlim_nofile_cur = 1024ULL;
static uint64_t g_rlim_nofile_max = 4096ULL;
static uint64_t g_rlim_default_cur = 16ULL * 1024ULL * 1024ULL;
static uint64_t g_rlim_default_max = 16ULL * 1024ULL * 1024ULL;

static void bfree_rlimit_defaults(long resource, uint64_t *cur, uint64_t *max)
{
    if (resource == 3) { /* RLIMIT_STACK */
        *cur = g_rlim_stack_cur;
        *max = g_rlim_stack_max;
    } else if (resource == 7) { /* RLIMIT_NOFILE */
        *cur = g_rlim_nofile_cur;
        *max = g_rlim_nofile_max;
    } else {
        *cur = g_rlim_default_cur;
        *max = g_rlim_default_max;
    }
}

static void bfree_rlimit_store(long resource, uint64_t cur, uint64_t max)
{
    if (resource == 3) {
        g_rlim_stack_cur = cur;
        g_rlim_stack_max = max;
    } else if (resource == 7) {
        g_rlim_nofile_cur = cur;
        g_rlim_nofile_max = max;
    } else {
        g_rlim_default_cur = cur;
        g_rlim_default_max = max;
    }
}

long sys_getrlimit(long resource, long rlim_ptr)
{
    typedef struct {
        uint64_t rlim_cur;
        uint64_t rlim_lim;
    } bfree_rlimit_t;
    bfree_rlimit_t *rlim = (bfree_rlimit_t *)(uintptr_t)rlim_ptr;

    if (rlim == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)rlim)) {
        return -14;
    }
    bfree_rlimit_defaults(resource, &rlim->rlim_cur, &rlim->rlim_lim);
    return 0;
}

/* --- Linux musl/Qt guest stubs (x86_64 syscall_arch.h numbers) --- */

#define BFREE_GUEST_PIPE_SLOTS      16
#define BFREE_GUEST_PIPE_BUF_SIZE   4096
#define BFREE_GUEST_PIPE_MAGIC_BASE 0x3400

typedef struct {
    int used;
    int nonblock;
    int wr_open;
    int rd_open;
    size_t len;
    unsigned char buf[BFREE_GUEST_PIPE_BUF_SIZE];
} bfree_guest_pipe_slot_t;

static bfree_guest_pipe_slot_t g_guest_pipes[BFREE_GUEST_PIPE_SLOTS];
static int bfree_guest_fd_resolve(int fd);
static void bfree_guest_ofd_maybe_release(int fd);

static int bfree_guest_pipe_magic_fd(int slot, int wr)
{
    return (int)BFREE_GUEST_PIPE_MAGIC_BASE + slot * 2 + (wr ? 1 : 0);
}

static int bfree_guest_pipe_slot_from_magic(int magic)
{
    int off;

    if (magic < (int)BFREE_GUEST_PIPE_MAGIC_BASE) {
        return -1;
    }
    off = magic - (int)BFREE_GUEST_PIPE_MAGIC_BASE;
    if (off < 0 || off >= BFREE_GUEST_PIPE_SLOTS * 2) {
        return -1;
    }
    return off / 2;
}

static int bfree_guest_pipe_is_wr_magic(int magic)
{
    int off = magic - (int)BFREE_GUEST_PIPE_MAGIC_BASE;

    return off >= 0 && ((off & 1) != 0);
}

static bfree_guest_pipe_slot_t *bfree_guest_pipe_slot_from_fd(int fd)
{
    int resolved;
    int slot;

    resolved = bfree_guest_fd_resolve(fd);
    slot = bfree_guest_pipe_slot_from_magic(resolved);
    if (slot < 0 || !g_guest_pipes[slot].used) {
        return 0;
    }
    return &g_guest_pipes[slot];
}

/* MSG_PEEK: copy without draining the pipe buffer. */
static long bfree_guest_pipe_peek(int fd, long buf, long count)
{
    bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd(fd);
    uint8_t *dst;
    size_t n;
    size_t i;

    if (!ps || buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if (ps->len == 0) {
        return ps->wr_open <= 0 ? 0 : -11;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    n = (size_t)count;
    if (n > ps->len) {
        n = ps->len;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = ps->buf[i];
    }
    return (long)n;
}

static void bfree_guest_pipe_ref(int resolved, int delta)
{
    int slot;

    slot = bfree_guest_pipe_slot_from_magic(resolved);
    if (slot < 0 || !g_guest_pipes[slot].used) {
        return;
    }
    if (bfree_guest_pipe_is_wr_magic(resolved)) {
        g_guest_pipes[slot].wr_open += delta;
        if (g_guest_pipes[slot].wr_open < 0) {
            g_guest_pipes[slot].wr_open = 0;
        }
    } else {
        g_guest_pipes[slot].rd_open += delta;
        if (g_guest_pipes[slot].rd_open < 0) {
            g_guest_pipes[slot].rd_open = 0;
        }
    }
}

static void bfree_guest_pipe_reset_all(void)
{
    int i;

    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        g_guest_pipes[i].used = 0;
        g_guest_pipes[i].nonblock = 0;
        g_guest_pipes[i].wr_open = 0;
        g_guest_pipes[i].rd_open = 0;
        g_guest_pipes[i].len = 0;
    }
}

static void bfree_guest_fork_child_pipe_close_writers(void)
{
    int i;

    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (g_guest_pipes[i].used) {
            g_guest_pipes[i].wr_open = 0;
        }
    }
}
static uint64_t g_guest_eventfd_val[BFREE_MAX_GUEST_EVENTFD];
static int g_guest_eventfd_nonblock[BFREE_MAX_GUEST_EVENTFD];
/* EFD_SEMAPHORE: read() yields 1 and decrements instead of draining. */
static int g_guest_eventfd_sem[BFREE_MAX_GUEST_EVENTFD];
static int g_guest_eventfd_next;

/* prctl PR_SET_PDEATHSIG: signal raised when the parent goes away (0 = none). */
static int g_guest_pdeathsig;

typedef struct {
    int used;
    int magic_fd;
    int nonblock;
    uint64_t mask;
} bfree_signalfd_t;
static bfree_signalfd_t g_guest_signalfds[BFREE_MAX_SIGNALFD];

#define BFREE_PTY_SLOTS             4
#define BFREE_PTY_BUF               512
#define BFREE_PTY_MASTER_BASE       0x3A00
#define BFREE_PTY_SLAVE_BASE        0x3A40

typedef struct {
    int used;
    int master_open;
    int slave_open;
    unsigned char m2s[BFREE_PTY_BUF];
    size_t m2s_len;
    unsigned char s2m[BFREE_PTY_BUF];
    size_t s2m_len;
} bfree_pty_t;
static bfree_pty_t g_guest_ptys[BFREE_PTY_SLOTS];

static int bfree_pty_slot_from_fd(int fd)
{
    if (fd >= (int)BFREE_PTY_MASTER_BASE &&
        fd < (int)BFREE_PTY_MASTER_BASE + BFREE_PTY_SLOTS) {
        return fd - (int)BFREE_PTY_MASTER_BASE;
    }
    if (fd >= (int)BFREE_PTY_SLAVE_BASE &&
        fd < (int)BFREE_PTY_SLAVE_BASE + BFREE_PTY_SLOTS) {
        return fd - (int)BFREE_PTY_SLAVE_BASE;
    }
    return -1;
}

static int bfree_pty_is_master(int fd)
{
    return fd >= (int)BFREE_PTY_MASTER_BASE &&
           fd < (int)BFREE_PTY_MASTER_BASE + BFREE_PTY_SLOTS;
}

static int bfree_user_ptr_mapped(long ptr);
static int bfree_user_buf_mapped(uint64_t base, uint64_t len);
static int copy_user_cstr(long user_ptr, char *out, size_t cap);
static int bfree_guest_is_pipe_rd(int fd);
static int bfree_guest_is_pipe_wr(int fd);

static int bfree_guest_is_eventfd(int fd)
{
    return fd >= (int)BFREE_GUEST_EVENTFD_BASE
        && fd < (int)BFREE_GUEST_EVENTFD_BASE + BFREE_MAX_GUEST_EVENTFD;
}

static int bfree_guest_eventfd_index(int fd)
{
    return fd - (int)BFREE_GUEST_EVENTFD_BASE;
}

#define BFREE_GUEST_DEV_TTY_FD     0x3707
#define BFREE_GUEST_DEV_NULL_FD     0x3700
#define BFREE_GUEST_DEV_URANDOM_FD  0x3701
#define BFREE_GUEST_PROC_MAPS_FD    0x3702
#define BFREE_GUEST_PASSWD_FD       0x3703
#define BFREE_GUEST_GROUP_FD        0x3704
#define BFREE_GUEST_PROFILE_FD      0x3705
#define BFREE_GUEST_BUSYBOX_FD      0x3706
#define BFREE_GUEST_VFILE_SLOTS     64
#define BFREE_GUEST_VFILE_SIZE      16384
/* Keep clear of dir magics (0x3700..0x3736), OFD (0x3800), UNIX (0x3900),
 * PTY (0x3A00), and INET (0x3B00). Prior 0x3710 collided with ROOT_DIR;
 * 0x3900 collided with AF_UNIX and broke lseek/fcntl on early vfile slots. */
#define BFREE_GUEST_VFILE_FD_BASE   0x3C00
#define BFREE_GUEST_ROOT_DIR_FD     0x3720
#define BFREE_GUEST_TMP_DIR_FD      0x3721
#define BFREE_GUEST_BIN_DIR_FD      0x3722
#define BFREE_GUEST_MOTD_FD         0x3723
#define BFREE_GUEST_HOSTS_FD        0x3734
#define BFREE_GUEST_RESOLV_FD       0x3735
#define BFREE_GUEST_USR_DIR_FD      0x3724
#define BFREE_GUEST_VAR_DIR_FD      0x3725
#define BFREE_GUEST_HOME_DIR_FD     0x3732
#define BFREE_GUEST_PERSIST_DIR_FD  0x3736
#define BFREE_GUEST_PTS_DIR_FD      0x3733
#define BFREE_GUEST_PROC_DIR_FD     0x3726
#define BFREE_GUEST_PROC_PID_DIR_FD 0x3727
#define BFREE_GUEST_PROC_PIDSTAT_FD 0x3728
#define BFREE_GUEST_PROC_CMDLINE_FD 0x3729
#define BFREE_GUEST_PROC_MEMINFO_FD 0x372a
#define BFREE_GUEST_PROC_UPTIME_FD  0x372b
#define BFREE_GUEST_PROC_LOADAVG_FD 0x372c
#define BFREE_GUEST_PROC_CPUSTAT_FD 0x372d
#define BFREE_GUEST_PROC_STATUS_FD  0x372e
#define BFREE_GUEST_PROC_MOUNTS_FD  0x372f
#define BFREE_GUEST_PROC_PID2STAT_FD 0x3730
#define BFREE_GUEST_PROC_PID2CMDLINE_FD 0x3731

#define BFREE_LINUX_O_ACCMODE       3
#define BFREE_LINUX_O_CREAT         0100
#define BFREE_LINUX_O_TRUNC         01000
#define BFREE_LINUX_O_APPEND        02000
#define BFREE_LINUX_O_CLOEXEC       02000000
#define BFREE_LINUX_O_NONBLOCK      04000
#ifndef BFREE_IN_MODIFY
#define BFREE_IN_MODIFY  0x00000002U
#define BFREE_IN_CREATE  0x00000100U
#define BFREE_IN_DELETE  0x00000200U
#endif
#define BFREE_INOTIFY_FD_BASE 98000
#define BFREE_MAX_INOTIFY     4
#define BFREE_LINUX_FD_CLOEXEC      1
#define BFREE_LINUX_O_DIRECTORY     0200000
#define BFREE_LINUX_O_PATH          010000000
#define BFREE_LINUX_O_TMPFILE       020000000

/* memfd_create flags and F_ADD_SEALS / F_GET_SEALS bits. */
#define BFREE_MFD_CLOEXEC       1
#define BFREE_MFD_ALLOW_SEALING 2
#define BFREE_F_SEAL_SEAL       0x0001
#define BFREE_F_SEAL_SHRINK     0x0002
#define BFREE_F_SEAL_GROW       0x0004
#define BFREE_F_SEAL_WRITE      0x0008


typedef struct {
    int used;
    int is_symlink; /* data[] holds the link target instead of file contents */
    int is_dir;     /* directory vnode under /tmp (name may contain '/') */
    int orphaned;   /* unlinked from namespace but still open */
    int open_refs;  /* live OFDs pointing at this vnode */
    int nlink;      /* directory entries (primary name + aliases) */
    char name[48];  /* path relative to /tmp, e.g. "a" or "a/b.txt" */
    unsigned char data[BFREE_GUEST_VFILE_SIZE];
    size_t len;
    /* Legacy fallback for internal magic fds. Published Linux fds use an
     * open-file description, so separate open() calls do not share this. */
    size_t pos;
    /* utimensat / futimens — reported by fstat */
    int64_t atime_sec;
    int64_t atime_nsec;
    int64_t mtime_sec;
    int64_t mtime_nsec;
    /* chmod/fchmod — 0 means "use type default" in stat */
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    /* F_ADD_SEALS bits (memfd) */
    uint32_t seals;
    /* Thin xattr: one name/value pair per vfile (ENODATA when empty). */
    char xattr_name[32];
    unsigned char xattr_value[64];
    size_t xattr_len;
} bfree_guest_vfile_t;

static bfree_guest_vfile_t g_guest_vfiles[BFREE_GUEST_VFILE_SLOTS];

/* MAP_SHARED vfile mmap: anon pages + writeback into vf->data on munmap. */
#define BFREE_GUEST_SHARED_MMAP_SLOTS 8
typedef struct {
    int used;
    int vfile_idx;
    uint64_t va;
    size_t map_len;
    size_t file_off;
} bfree_guest_shared_mmap_t;
static bfree_guest_shared_mmap_t g_guest_shared_mmaps[BFREE_GUEST_SHARED_MMAP_SLOTS];

static void bfree_guest_shared_mmap_writeback_one(bfree_guest_shared_mmap_t *sm)
{
    bfree_guest_vfile_t *vf;
    size_t i;
    size_t n;
    size_t cap;
    const uint8_t *src;

    if (!sm || !sm->used) {
        return;
    }
    if (sm->vfile_idx < 0 || sm->vfile_idx >= BFREE_GUEST_VFILE_SLOTS) {
        sm->used = 0;
        return;
    }
    vf = &g_guest_vfiles[sm->vfile_idx];
    if (!vf->used || vf->is_dir) {
        sm->used = 0;
        return;
    }
    cap = BFREE_GUEST_VFILE_SIZE;
    if (sm->file_off >= cap) {
        sm->used = 0;
        return;
    }
    n = sm->map_len;
    if (n > cap - sm->file_off) {
        n = cap - sm->file_off;
    }
    src = (const uint8_t *)(uintptr_t)sm->va;
    for (i = 0; i < n; ++i) {
        vf->data[sm->file_off + i] = src[i];
    }
    if (sm->file_off + n > vf->len) {
        vf->len = sm->file_off + n;
    }
}

static void bfree_guest_shared_mmap_flush_range(uint64_t lo, uint64_t hi)
{
    int i;

    for (i = 0; i < BFREE_GUEST_SHARED_MMAP_SLOTS; ++i) {
        bfree_guest_shared_mmap_t *sm = &g_guest_shared_mmaps[i];
        uint64_t smo;
        uint64_t smhi;

        if (!sm->used) {
            continue;
        }
        smo = sm->va;
        smhi = smo + (uint64_t)sm->map_len;
        /* Any overlap with munmap range → writeback and drop tracking. */
        if (smhi > lo && smo < hi) {
            bfree_guest_shared_mmap_writeback_one(sm);
            sm->used = 0;
        }
    }
}

/* Keep read() coherent with live MAP_SHARED mappings (no page-fault write-through). */
static void bfree_guest_shared_mmap_sync_vfile(int vfile_idx)
{
    int i;

    if (vfile_idx < 0 || vfile_idx >= BFREE_GUEST_VFILE_SLOTS) {
        return;
    }
    for (i = 0; i < BFREE_GUEST_SHARED_MMAP_SLOTS; ++i) {
        bfree_guest_shared_mmap_t *sm = &g_guest_shared_mmaps[i];

        if (sm->used && sm->vfile_idx == vfile_idx) {
            bfree_guest_shared_mmap_writeback_one(sm);
        }
    }
}

/* Linux msync: flush live MAP_SHARED pages into vfile; keep tracking (unlike munmap). */
static long sys_linux_msync(long addr, long length, long flags)
{
    uint64_t lo;
    uint64_t hi;
    int i;

    (void)flags;
    if (length < 0) {
        return -22;
    }
    if (addr == 0 && length == 0) {
        return 0;
    }
    if (length == 0) {
        return 0;
    }
    lo = (uint64_t)(uintptr_t)addr;
    hi = lo + (uint64_t)length;
    if (hi < lo) {
        return -22;
    }
    for (i = 0; i < BFREE_GUEST_SHARED_MMAP_SLOTS; ++i) {
        bfree_guest_shared_mmap_t *sm = &g_guest_shared_mmaps[i];
        uint64_t smo;
        uint64_t smhi;

        if (!sm->used) {
            continue;
        }
        smo = sm->va;
        smhi = smo + (uint64_t)sm->map_len;
        if (smhi > lo && smo < hi) {
            bfree_guest_shared_mmap_writeback_one(sm);
            /* Keep sm->used — mapping remains live after msync. */
        }
    }
    return 0;
}

static int bfree_guest_shared_mmap_track(int vfile_idx, uint64_t va, size_t map_len,
                                         size_t file_off)
{
    int i;

    for (i = 0; i < BFREE_GUEST_SHARED_MMAP_SLOTS; ++i) {
        if (!g_guest_shared_mmaps[i].used) {
            g_guest_shared_mmaps[i].used = 1;
            g_guest_shared_mmaps[i].vfile_idx = vfile_idx;
            g_guest_shared_mmaps[i].va = va;
            g_guest_shared_mmaps[i].map_len = map_len;
            g_guest_shared_mmaps[i].file_off = file_off;
            return 0;
        }
    }
    return -1; /* table full — mapping still usable as PRIVATE-like copy */
}

/* Stamp VMM_PTE_SHARED (bit 10) on live MAP_SHARED pages so fork keeps RW. */
static void bfree_guest_shared_mmap_stamp_ptes(uint64_t va, size_t map_len)
{
    page_table_t *pt;
    size_t off;

    if (map_len == 0 || !knl_current_task || !knl_current_task->page_table_base) {
        return;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    va &= ~(PAGE_SIZE - 1ULL);
    map_len = (map_len + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    for (off = 0; off < map_len; off += (size_t)PAGE_SIZE) {
        uint64_t page_va = va + (uint64_t)off;
        uint64_t phys = 0;

        if (vmm_user_virt_to_phys(pt, page_va, &phys) != 0) {
            continue;
        }
        (void)vmm_map_page(pt, page_va, phys, 0x007ULL | (1ULL << 10));
    }
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
}

/* Slice 2: find a live MAP_SHARED track overlapping the same vfile file range. */
static bfree_guest_shared_mmap_t *bfree_guest_shared_mmap_find_overlap(int vfile_idx,
                                                                      size_t file_off,
                                                                      size_t map_len)
{
    int i;
    size_t new_hi;

    if (vfile_idx < 0 || map_len == 0) {
        return 0;
    }
    new_hi = file_off + map_len;
    for (i = 0; i < BFREE_GUEST_SHARED_MMAP_SLOTS; ++i) {
        bfree_guest_shared_mmap_t *sm = &g_guest_shared_mmaps[i];
        size_t sm_hi;

        if (!sm->used || sm->vfile_idx != vfile_idx) {
            continue;
        }
        sm_hi = sm->file_off + sm->map_len;
        if (sm_hi > file_off && sm->file_off < new_hi) {
            return sm;
        }
    }
    return 0;
}

/*
 * Remap new_va pages onto the same physical frames as donor for overlapping
 * file offsets. Fresh anon pages from the preceding mmap are orphaned (munmap
 * never frees guest heap phys today) — acceptable for minimal live SHARED.
 * Returns 0 if at least one page was aliased, -1 otherwise.
 */
static int bfree_guest_shared_mmap_alias_pages(uint64_t new_va, size_t new_file_off,
                                              size_t map_len,
                                              const bfree_guest_shared_mmap_t *donor)
{
    page_table_t *pt;
    size_t off;
    int aliased = 0;

    if (!donor || map_len == 0 || !knl_current_task ||
        !knl_current_task->page_table_base) {
        return -1;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    new_va &= ~(PAGE_SIZE - 1ULL);
    map_len = (map_len + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);

    for (off = 0; off < map_len; off += (size_t)PAGE_SIZE) {
        size_t file_byte = new_file_off + off;
        uint64_t src_va;
        uint64_t dst_va;
        uint64_t phys = 0;

        if (file_byte < donor->file_off) {
            continue;
        }
        if (file_byte >= donor->file_off + donor->map_len) {
            continue;
        }
        src_va = donor->va + (uint64_t)(file_byte - donor->file_off);
        src_va &= ~(PAGE_SIZE - 1ULL);
        dst_va = new_va + (uint64_t)off;
        if (vmm_user_virt_to_phys(pt, src_va, &phys) != 0) {
            continue;
        }
        (void)vmm_unmap_page(pt, dst_va);
        if (vmm_map_page(pt, dst_va, phys, 0x007ULL | (1ULL << 10)) != 0) {
            return -1;
        }
        aliased = 1;
    }
    if (aliased) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
        return 0;
    }
    return -1;
}

#define BFREE_GUEST_ALIAS_SLOTS 32
typedef struct {
    int used;
    int vnode; /* index into g_guest_vfiles */
    char name[48];
} bfree_guest_alias_t;
static bfree_guest_alias_t g_guest_aliases[BFREE_GUEST_ALIAS_SLOTS];

#define BFREE_GUEST_OFD_SLOTS       64
#define BFREE_GUEST_OFD_FD_BASE     0x3800

typedef struct {
    int used;
    int target;
    int flags;
    size_t pos;          /* file offset, or getdents64 index for directories */
    uint64_t dir_cookie; /* d_off cookie for directory streams */
} bfree_guest_ofd_t;

static bfree_guest_ofd_t g_guest_ofds[BFREE_GUEST_OFD_SLOTS];

#define BFREE_GUEST_FD_TABLE_SIZE 64
static int g_guest_fd_target[BFREE_GUEST_FD_TABLE_SIZE];
/* Resolved target captured at dup(); survives close() until dup2() consumes it. */
static int g_guest_fd_dup_save[BFREE_GUEST_FD_TABLE_SIZE];
static uint8_t g_guest_fd_cloexec[BFREE_GUEST_FD_TABLE_SIZE];
static int g_guest_fd_inited;

/* Whole-file advisory locks (F_SETLK / F_GETLK) keyed by vnode index. */
#define BFREE_GUEST_FLOCK_SLOTS 16
#define BFREE_LINUX_F_RDLCK 0
#define BFREE_LINUX_F_WRLCK 1
#define BFREE_LINUX_F_UNLCK 2
#define BFREE_LINUX_F_GETLK 5
#define BFREE_LINUX_F_SETLK 6
#define BFREE_LINUX_F_SETLKW 7
typedef struct {
    int used;
    int vnode; /* index into g_guest_vfiles */
    int type;  /* F_RDLCK / F_WRLCK */
    int owner_pid;
} bfree_guest_flock_t;
static bfree_guest_flock_t g_guest_flocks[BFREE_GUEST_FLOCK_SLOTS];

/* Seek positions for fds that are not vfile/OFD/magic blobs (last resort). */
#define BFREE_GUEST_SEEK_SLOTS 64
static struct {
    int used;
    int key;
    size_t pos;
} g_guest_seek_pos[BFREE_GUEST_SEEK_SLOTS];

static size_t *bfree_guest_seek_pos_for(int key)
{
    int i;
    int free_i = -1;

    for (i = 0; i < BFREE_GUEST_SEEK_SLOTS; ++i) {
        if (g_guest_seek_pos[i].used && g_guest_seek_pos[i].key == key) {
            return &g_guest_seek_pos[i].pos;
        }
        if (!g_guest_seek_pos[i].used && free_i < 0) {
            free_i = i;
        }
    }
    if (free_i < 0) {
        return 0;
    }
    g_guest_seek_pos[free_i].used = 1;
    g_guest_seek_pos[free_i].key = key;
    g_guest_seek_pos[free_i].pos = 0;
    return &g_guest_seek_pos[free_i].pos;
}

static void bfree_guest_flocks_drop_pid(int pid)
{
    int i;

    if (pid <= 0) {
        return;
    }
    for (i = 0; i < BFREE_GUEST_FLOCK_SLOTS; ++i) {
        if (g_guest_flocks[i].used && g_guest_flocks[i].owner_pid == pid) {
            g_guest_flocks[i].used = 0;
        }
    }
}

/* musl/glibc x86_64 struct flock layout (with off_t alignment padding). */
typedef struct {
    int16_t l_type;
    int16_t l_whence;
    int32_t __pad0;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int32_t __pad1;
} bfree_linux_flock_t;

static void bfree_guest_fd_ensure_init(void)
{
    int i;

    if (g_guest_fd_inited) {
        return;
    }
    for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        g_guest_fd_target[i] = -1;
        g_guest_fd_dup_save[i] = -1;
    }
    /*
     * Emulate “stdin/stdout/stderr are always open” like a normal process.
     * This also lets ash's background-job logic do:
     *   close(0); open("/dev/null", O_RDONLY);
     * and expect open() to return fd==0.
     */
    g_guest_fd_target[0] = 0;
    g_guest_fd_target[1] = 1;
    g_guest_fd_target[2] = 2;
    g_guest_fd_inited = 1;
}

static void bfree_guest_stdio_heal_pipes(void)
{
    int i;

    bfree_guest_fd_ensure_init();
    for (i = 0; i <= 2; ++i) {
        int resolved;

        if (g_guest_fd_target[i] < 0) {
            continue;
        }
        resolved = g_guest_fd_target[i];
        if (bfree_guest_pipe_slot_from_magic(resolved) >= 0) {
            /* Drop the pipeline endpoint; identity 0/1/2 is the console. */
            bfree_guest_pipe_ref(resolved, -1);
            g_guest_fd_target[i] = -1;
            g_guest_fd_dup_save[i] = -1;
        }
    }
    /* Resync refcounts from the live fd table, then free unused slots. */
    bfree_guest_pipe_reclaim_dead_slots();
}

static int bfree_guest_fd_resolve(int fd)
{
    bfree_guest_fd_ensure_init();
    if (fd < 0 || fd >= BFREE_GUEST_FD_TABLE_SIZE) {
        return fd;
    }
    /* One hop only: dup(1) saves stdout as table[3]=1; after dup2(file,1), dup2(3,1)
     * must restore 1, not follow table[1] still pointing at the redirect target. */
    if (g_guest_fd_target[fd] >= 0) {
        return g_guest_fd_target[fd];
    }
    return fd;
}

static int bfree_guest_fd_dup2_resolve_old(int oldfd)
{
    int resolved;

    resolved = bfree_guest_fd_resolve(oldfd);
    if (oldfd >= 0 && oldfd < BFREE_GUEST_FD_TABLE_SIZE &&
        g_guest_fd_target[oldfd] < 0 &&
        g_guest_fd_dup_save[oldfd] >= 0) {
        resolved = g_guest_fd_dup_save[oldfd];
    }
    return resolved;
}

static void bfree_guest_fd_apply_dup2(int oldfd, int newfd, int resolved)
{
    int prev;

    if (newfd < 0 || newfd >= BFREE_GUEST_FD_TABLE_SIZE) {
        return;
    }
    prev = bfree_guest_fd_resolve(newfd);
    if (prev >= 0 && prev != resolved &&
        (bfree_guest_is_pipe_wr(prev) || bfree_guest_is_pipe_rd(prev))) {
        bfree_guest_pipe_ref(prev, -1);
    }
    if ((newfd == 1 || newfd == 2) && (resolved == 1 || resolved == 2)) {
        g_guest_fd_target[newfd] = -1;
    } else {
        g_guest_fd_target[newfd] = resolved;
        if (bfree_guest_is_pipe_wr(resolved) || bfree_guest_is_pipe_rd(resolved)) {
            bfree_guest_pipe_ref(resolved, 1);
        }
    }
    if (oldfd >= 0 && oldfd < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_dup_save[oldfd] = -1;
    }
    bfree_guest_ofd_maybe_release(prev);
}

/* Return a small Linux-like fd (3..63) that resolves to target (may be magic guest fd). */
static int bfree_guest_fd_publish(int target)
{
    int i;
    int start;

    bfree_guest_fd_ensure_init();
    if (target < 0) {
        return target;
    }
    if (target < BFREE_GUEST_FD_TABLE_SIZE &&
        g_guest_fd_target[target] < 0 &&
        g_guest_fd_dup_save[target] < 0 &&
        target <= 2) {
        return target;
    }
    /*
     * Publish the smallest free fd slot, but never 0..2: handing a regular
     * file out on the stdin/stdout/stderr slots aliases the console and
     * corrupts the whole shell session (lost output, files read as input)
     * whenever a vfork child left one of those slots closed in the shared
     * fd table. Redirections still work because ash uses open()+dup2().
     */
    start = 3;
    if (target >= 3 && target < BFREE_GUEST_FD_TABLE_SIZE) {
        start = target;
    }
    for (i = start; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        /* RLIMIT_NOFILE: fd number must be < soft limit (Linux: max_fd+1). */
        if ((uint64_t)i >= g_rlim_nofile_cur) {
            return -24; /* EMFILE */
        }
        if (g_guest_fd_target[i] < 0 && g_guest_fd_dup_save[i] < 0) {
            g_guest_fd_target[i] = target;
            g_guest_fd_cloexec[i] = 0;
            return i;
        }
    }
    return -24;
}

static int bfree_guest_is_vfile_fd(int fd)
{
    return fd >= (int)BFREE_GUEST_VFILE_FD_BASE &&
           fd < (int)BFREE_GUEST_VFILE_FD_BASE + BFREE_GUEST_VFILE_SLOTS;
}

static bfree_guest_vfile_t *bfree_guest_vfile_from_fd(int fd)
{
    int idx;

    if (!bfree_guest_is_vfile_fd(fd)) {
        return 0;
    }
    idx = fd - (int)BFREE_GUEST_VFILE_FD_BASE;
    if (idx < 0 || idx >= BFREE_GUEST_VFILE_SLOTS || !g_guest_vfiles[idx].used) {
        return 0;
    }
    return &g_guest_vfiles[idx];
}

static bfree_guest_ofd_t *bfree_guest_ofd_from_fd(int fd)
{
    int idx = fd - (int)BFREE_GUEST_OFD_FD_BASE;

    if (idx < 0 || idx >= BFREE_GUEST_OFD_SLOTS || !g_guest_ofds[idx].used) {
        return 0;
    }
    return &g_guest_ofds[idx];
}

static bfree_guest_vfile_t *bfree_guest_vfile_from_open_fd(
    int fd, bfree_guest_ofd_t **ofd_out)
{
    bfree_guest_ofd_t *ofd = bfree_guest_ofd_from_fd(fd);

    if (ofd_out) {
        *ofd_out = ofd;
    }
    if (ofd) {
        return bfree_guest_vfile_from_fd(ofd->target);
    }
    return bfree_guest_vfile_from_fd(fd);
}

static long bfree_guest_mmap_vfile(long addr, long length, long flags, long fd, long offset)
{
    bfree_guest_ofd_t *ofd = 0;
    int resolved = bfree_guest_fd_resolve((int)fd);
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_open_fd(resolved, &ofd);
    long mapped;
    size_t copy_n;
    size_t i;
    size_t off;
    uint8_t *dst;
    int shared;
    int vidx;

    (void)ofd;
    if (!vf || vf->is_dir) {
        return -19; /* ENODEV */
    }
    if (offset < 0) {
        return -22; /* EINVAL */
    }
    off = (size_t)offset;
    if (off > vf->len) {
        return -22; /* EINVAL — past EOF */
    }
    shared = ((flags & MAP_SHARED) != 0) && ((flags & MAP_PRIVATE) == 0);
    /* Always materialize as anon pages; MAP_SHARED additionally tracks writeback. */
    mapped = sys_mmap_anonymous_heap(addr, length, flags | MAP_ANONYMOUS);
    if (mapped < 0) {
        return mapped;
    }
    vidx = (int)(vf - g_guest_vfiles);
    /* Slice 2: second MAP_SHARED of same vfile range aliases first's phys pages
     * so in-process peers see writes without msync. */
    if (shared && length > 0) {
        bfree_guest_shared_mmap_t *donor =
            bfree_guest_shared_mmap_find_overlap(vidx, off, (size_t)length);

        if (donor &&
            bfree_guest_shared_mmap_alias_pages((uint64_t)(uintptr_t)mapped, off,
                                               (size_t)length, donor) == 0) {
            (void)bfree_guest_shared_mmap_track(vidx, (uint64_t)(uintptr_t)mapped,
                                                (size_t)length, off);
            bfree_guest_shared_mmap_stamp_ptes((uint64_t)(uintptr_t)mapped,
                                               (size_t)length);
            return mapped;
        }
    }
    copy_n = vf->len - off;
    if (length > 0 && (size_t)length < copy_n) {
        copy_n = (size_t)length;
    }
    dst = (uint8_t *)(uintptr_t)(uint64_t)mapped;
    for (i = 0; i < copy_n; ++i) {
        dst[i] = vf->data[off + i];
    }
    if (shared && length > 0) {
        (void)bfree_guest_shared_mmap_track(vidx, (uint64_t)(uintptr_t)mapped,
                                            (size_t)length, off);
        bfree_guest_shared_mmap_stamp_ptes((uint64_t)(uintptr_t)mapped,
                                           (size_t)length);
    }
    return mapped;
}



static void bfree_guest_vfile_clear_slot(bfree_guest_vfile_t *vf)
{
    int i;
    int vidx;

    if (!vf) {
        return;
    }
    vidx = (int)(vf - g_guest_vfiles);
    for (i = 0; i < BFREE_GUEST_ALIAS_SLOTS; ++i) {
        if (g_guest_aliases[i].used && g_guest_aliases[i].vnode == vidx) {
            g_guest_aliases[i].used = 0;
            g_guest_aliases[i].name[0] = '\0';
        }
    }
    vf->used = 0;
    vf->is_symlink = 0;
    vf->is_dir = 0;
    vf->orphaned = 0;
    vf->open_refs = 0;
    vf->nlink = 0;
    vf->name[0] = '\0';
    vf->len = 0;
    vf->pos = 0;
    vf->atime_sec = 0;
    vf->atime_nsec = 0;
    vf->mtime_sec = 0;
    vf->mtime_nsec = 0;
    vf->mode = 0;
    vf->uid = 0;
    vf->gid = 0;
    vf->seals = 0;
    vf->xattr_name[0] = '\0';
    vf->xattr_len = 0;
}

static void bfree_guest_vfile_ref(int target, int delta)
{
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_fd(target);

    if (!vf) {
        return;
    }
    vf->open_refs += delta;
    if (vf->open_refs < 0) {
        vf->open_refs = 0;
    }
    if (vf->orphaned && vf->open_refs == 0) {
        bfree_guest_vfile_clear_slot(vf);
    }
}

static void bfree_guest_ofd_maybe_release(int fd)
{
    bfree_guest_ofd_t *ofd = bfree_guest_ofd_from_fd(fd);
    int i;

    if (!ofd) {
        return;
    }
    for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        if (g_guest_fd_target[i] == fd || g_guest_fd_dup_save[i] == fd) {
            return;
        }
        /* Parked coop side still holds this OFD — do not free under the child. */
        if (g_fd_snap_parent[i] == fd || g_fd_dup_save_snap_parent[i] == fd ||
            g_fd_snap_child[i] == fd || g_fd_dup_save_snap_child[i] == fd) {
            return;
        }
    }
    bfree_guest_vfile_ref(ofd->target, -1);
    memset(ofd, 0, sizeof(*ofd));
}

static int bfree_guest_vfile_publish_open(int target, int flags, size_t pos)
{
    int i;
    int published;

    for (i = 0; i < BFREE_GUEST_OFD_SLOTS; ++i) {
        if (!g_guest_ofds[i].used) {
            g_guest_ofds[i].used = 1;
            g_guest_ofds[i].target = target;
            g_guest_ofds[i].flags = flags;
            g_guest_ofds[i].pos = pos;
            g_guest_ofds[i].dir_cookie = 0;
            published = bfree_guest_fd_publish(BFREE_GUEST_OFD_FD_BASE + i);
            if (published < 0) {
                memset(&g_guest_ofds[i], 0, sizeof(g_guest_ofds[i]));
            } else {
                bfree_guest_vfile_ref(target, 1);
            }
            return published;
        }
    }
    return -24; /* EMFILE */
}

static bfree_guest_vfile_t *bfree_guest_vfile_find_by_name(const char *name)
{
    int i;

    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        if (g_guest_vfiles[i].used &&
            !g_guest_vfiles[i].orphaned &&
            strcmp(g_guest_vfiles[i].name, name) == 0) {
            return &g_guest_vfiles[i];
        }
    }
    for (i = 0; i < BFREE_GUEST_ALIAS_SLOTS; ++i) {
        int v;
        if (!g_guest_aliases[i].used) {
            continue;
        }
        if (strcmp(g_guest_aliases[i].name, name) != 0) {
            continue;
        }
        v = g_guest_aliases[i].vnode;
        if (v >= 0 && v < BFREE_GUEST_VFILE_SLOTS &&
            g_guest_vfiles[v].used && !g_guest_vfiles[v].orphaned) {
            return &g_guest_vfiles[v];
        }
    }
    return 0;
}

static int bfree_guest_alias_add(const char *name, int vnode)
{
    int i;
    size_t n = 0;

    if (!name || name[0] == '\0' || vnode < 0 || vnode >= BFREE_GUEST_VFILE_SLOTS) {
        return -22;
    }
    for (i = 0; i < BFREE_GUEST_ALIAS_SLOTS; ++i) {
        if (g_guest_aliases[i].used) {
            continue;
        }
        g_guest_aliases[i].used = 1;
        g_guest_aliases[i].vnode = vnode;
        while (name[n] != '\0' && n + 1U < sizeof(g_guest_aliases[i].name)) {
            g_guest_aliases[i].name[n] = name[n];
            ++n;
        }
        g_guest_aliases[i].name[n] = '\0';
        return 0;
    }
    return -28; /* ENOSPC */
}

static int bfree_guest_alias_remove_name(const char *name)
{
    int i;
    for (i = 0; i < BFREE_GUEST_ALIAS_SLOTS; ++i) {
        if (g_guest_aliases[i].used &&
            strcmp(g_guest_aliases[i].name, name) == 0) {
            g_guest_aliases[i].used = 0;
            g_guest_aliases[i].name[0] = '\0';
            return 1;
        }
    }
    return 0;
}

static int bfree_guest_vfile_alloc_slot(const char *name, int truncate)
{
    int i;
    bfree_guest_vfile_t *vf;

    vf = bfree_guest_vfile_find_by_name(name);
    if (vf) {
        if (truncate) {
            vf->len = 0;
            vf->pos = 0;
        }
        return (int)BFREE_GUEST_VFILE_FD_BASE + (int)(vf - g_guest_vfiles);
    }
    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        if (!g_guest_vfiles[i].used) {
            size_t n = 0;

            g_guest_vfiles[i].used = 1;
            g_guest_vfiles[i].orphaned = 0;
            g_guest_vfiles[i].open_refs = 0;
            g_guest_vfiles[i].nlink = 1;
            while (name[n] != '\0' && n + 1U < sizeof(g_guest_vfiles[i].name)) {
                g_guest_vfiles[i].name[n] = name[n];
                ++n;
            }
            g_guest_vfiles[i].name[n] = '\0';
            g_guest_vfiles[i].len = 0;
            g_guest_vfiles[i].pos = 0;
            g_guest_vfiles[i].is_symlink = 0;
            g_guest_vfiles[i].is_dir = 0;
            g_guest_vfiles[i].atime_sec = 0;
            g_guest_vfiles[i].atime_nsec = 0;
            g_guest_vfiles[i].mtime_sec = 0;
            g_guest_vfiles[i].mtime_nsec = 0;
            g_guest_vfiles[i].mode = 0;
            g_guest_vfiles[i].uid = 0;
            g_guest_vfiles[i].gid = 0;
            g_guest_vfiles[i].seals = 0;
            return (int)BFREE_GUEST_VFILE_FD_BASE + i;
        }
    }
    return -24;
}

/* ---- F1: ATA PIO (primary master) + /persist disk store ---------------- */
/*
 * QEMU wiring: -drive file=persist.img,if=ide,index=0,media=disk,format=raw
 * (the boot CD stays on the secondary channel via -cdrom).
 *
 * On-disk layout (512B sectors, LBA28, polling PIO with nIEN):
 *   LBA 0            : "BFP1" magic (u32 LE) + u32 record count
 *   LBA 1 + i*33     : record i header — name[48] + u32 len
 *   LBA 2 + i*33 ..  : record i data (32 sectors = BFREE_GUEST_VFILE_SIZE)
 * Records mirror g_guest_vfiles entries whose name starts with "persist/".
 */
#define BFREE_ATA_IO_BASE   0x1F0
#define BFREE_ATA_CTRL      0x3F6
#define BFREE_PERSIST_MAGIC 0x31504642u /* "BFP1" LE */
#define BFREE_PERSIST_SECS_PER_REC 33u

static int g_persist_disk_state; /* 0=unprobed 1=ready -1=absent */
static int g_persist_loaded;
static int g_persist_fat_mounted; /* 1 = FAT RW mirrored into vfiles */

static inline uint8_t bfree_ata_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void bfree_ata_outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

static inline uint16_t bfree_ata_inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void bfree_ata_outw(uint16_t port, uint16_t v)
{
    __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port));
}

/* Wait for BSY clear; optionally require DRQ. Bounded spin (no IRQs). */
static int bfree_ata_wait(int want_drq)
{
    unsigned long spins;

    for (spins = 0; spins < 4000000UL; ++spins) {
        uint8_t st = bfree_ata_inb(BFREE_ATA_IO_BASE + 7);
        if (st == 0xFF) {
            return -1; /* floating bus */
        }
        if (st & 0x01) {
            return -1; /* ERR */
        }
        if (!(st & 0x80)) { /* !BSY */
            if (!want_drq || (st & 0x08)) {
                return 0;
            }
        }
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

static int bfree_ata_probe(void)
{
    if (g_persist_disk_state != 0) {
        return g_persist_disk_state;
    }
    /* nIEN: poll, never raise IRQ14. */
    bfree_ata_outb(BFREE_ATA_CTRL, 0x02);
    bfree_ata_outb(BFREE_ATA_IO_BASE + 6, 0xE0); /* primary master, LBA */
    if (bfree_ata_inb(BFREE_ATA_IO_BASE + 7) == 0xFF || bfree_ata_wait(0) != 0) {
        g_persist_disk_state = -1;
        uart_puts("[PERSIST] no ATA disk\n");
        return -1;
    }
    g_persist_disk_state = 1;
    uart_puts("[PERSIST] ATA disk ready\n");
    return 1;
}

static int bfree_ata_rw_sector(uint32_t lba, void *buf, int write)
{
    uint16_t *p = (uint16_t *)buf;
    int i;

    if (bfree_ata_probe() != 1) {
        return -1;
    }
    if (bfree_ata_wait(0) != 0) {
        return -1;
    }
    bfree_ata_outb(BFREE_ATA_IO_BASE + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    bfree_ata_outb(BFREE_ATA_IO_BASE + 2, 1);
    bfree_ata_outb(BFREE_ATA_IO_BASE + 3, (uint8_t)lba);
    bfree_ata_outb(BFREE_ATA_IO_BASE + 4, (uint8_t)(lba >> 8));
    bfree_ata_outb(BFREE_ATA_IO_BASE + 5, (uint8_t)(lba >> 16));
    bfree_ata_outb(BFREE_ATA_IO_BASE + 7, write ? 0x30 : 0x20);
    if (bfree_ata_wait(1) != 0) {
        return -1;
    }
    if (write) {
        for (i = 0; i < 256; ++i) {
            bfree_ata_outw(BFREE_ATA_IO_BASE, p[i]);
        }
        bfree_ata_outb(BFREE_ATA_IO_BASE + 7, 0xE7); /* FLUSH CACHE */
        if (bfree_ata_wait(0) != 0) {
            return -1;
        }
    } else {
        for (i = 0; i < 256; ++i) {
            p[i] = bfree_ata_inw(BFREE_ATA_IO_BASE);
        }
    }
    return 0;
}

static uint8_t g_persist_sec[512];

#if BFREE_PERSIST_FAT_PROBE
#include "persist_fat_probe.h"
#include "persist_fat_mount.h"

static int bfree_persist_ata_read_cb(uint32_t lba, void *buf512)
{
    return bfree_ata_rw_sector(lba, buf512, 0);
}

static int bfree_persist_ata_write_cb(uint32_t lba, const void *buf512)
{
    /* ATA helper takes non-const; sector buffer is only read for writes. */
    return bfree_ata_rw_sector(lba, (void *)buf512, 1);
}
#endif

/* Rewrite the whole store from live persist/ vfiles (small: ≤16 recs). */
static void bfree_persist_flush_all(void)
{
    uint32_t rec = 0;
    int i;

#if BFREE_PERSIST_FAT_PROBE
    if (g_persist_fat_mounted) {
        /* FAT RW: mirror each persist/ vfile as a root 8.3 file (1 cluster). */
        for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
            bfree_guest_vfile_t *vf = &g_guest_vfiles[i];
            const char *base;
            if (!vf->used || vf->orphaned || vf->is_dir || vf->is_symlink)
                continue;
            if (strncmp(vf->name, "persist/", 8) != 0)
                continue;
            base = vf->name + 8;
            if (!base[0])
                continue;
            if (bfree_persist_fat_put_root_file(bfree_persist_ata_read_cb,
                                               bfree_persist_ata_write_cb,
                                               base, vf->data,
                                               (uint32_t)vf->len) != 0) {
                uart_puts("[PERSIST] FAT put failed for ");
                uart_puts(base);
                uart_puts("\n");
            }
        }
        return;
    }
#endif
    if (bfree_ata_probe() != 1) {
        return;
    }
    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        bfree_guest_vfile_t *vf = &g_guest_vfiles[i];
        uint32_t base;
        uint32_t s;
        size_t off;

        if (!vf->used || vf->orphaned || vf->is_dir || vf->is_symlink) {
            continue;
        }
        if (strncmp(vf->name, "persist/", 8) != 0) {
            continue;
        }
        base = 1u + rec * BFREE_PERSIST_SECS_PER_REC;
        memset(g_persist_sec, 0, sizeof(g_persist_sec));
        memcpy(g_persist_sec, vf->name, sizeof(vf->name));
        *(uint32_t *)(g_persist_sec + 48) = (uint32_t)vf->len;
        if (bfree_ata_rw_sector(base, g_persist_sec, 1) != 0) {
            return;
        }
        for (s = 0, off = 0; off < vf->len; ++s, off += 512) {
            size_t chunk = vf->len - off;
            if (chunk > 512) {
                chunk = 512;
            }
            memset(g_persist_sec, 0, sizeof(g_persist_sec));
            memcpy(g_persist_sec, vf->data + off, chunk);
            if (bfree_ata_rw_sector(base + 1u + s, g_persist_sec, 1) != 0) {
                return;
            }
        }
        ++rec;
    }
    memset(g_persist_sec, 0, sizeof(g_persist_sec));
    *(uint32_t *)(g_persist_sec + 0) = BFREE_PERSIST_MAGIC;
    *(uint32_t *)(g_persist_sec + 4) = rec;
    (void)bfree_ata_rw_sector(0, g_persist_sec, 1);
}

#if BFREE_PERSIST_FAT_PROBE
static int bfree_persist_fat_on_file(const char *short_name, const uint8_t *data,
                                    uint32_t size, void *ctx)
{
    char name[64];
    int fd;
    bfree_guest_vfile_t *vf;
    size_t i;
    (void)ctx;
    if (!short_name || !short_name[0] || !data || size == 0)
        return 0;
    if (size > BFREE_GUEST_VFILE_SIZE)
        size = BFREE_GUEST_VFILE_SIZE;
    name[0] = 'p'; name[1] = 'e'; name[2] = 'r'; name[3] = 's';
    name[4] = 'i'; name[5] = 's'; name[6] = 't'; name[7] = '/';
    for (i = 0; short_name[i] && i + 9U < sizeof(name); ++i) {
        char c = short_name[i];
        /* Store lowercase for path match convenience; keep dots. */
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        name[8 + i] = c;
    }
    name[8 + i] = '\0';
    fd = bfree_guest_vfile_alloc_slot(name, 1);
    if (fd < 0)
        return -1;
    vf = &g_guest_vfiles[fd - (int)BFREE_GUEST_VFILE_FD_BASE];
    for (i = 0; i < size; ++i)
        vf->data[i] = data[i];
    vf->len = size;
    uart_puts("[PERSIST] FAT file ");
    uart_puts(name);
    uart_puts("\n");
    return 0;
}
#endif

static void bfree_persist_load_once(void)
{
    uint32_t count;
    uint32_t r;

    if (g_persist_loaded) {
        return;
    }
    g_persist_loaded = 1;
    g_persist_fat_mounted = 0;
    if (bfree_ata_probe() != 1) {
        return;
    }
    if (bfree_ata_rw_sector(0, g_persist_sec, 0) != 0) {
        return;
    }
#if BFREE_PERSIST_FAT_PROBE
    {
        int fat = bfree_persist_fat_probe(g_persist_sec);
        if (fat == 12 || fat == 16) {
            int mounted = bfree_persist_fat_mount_ro(bfree_persist_ata_read_cb,
                                                     bfree_persist_fat_on_file,
                                                     0);
            if (mounted == 12 || mounted == 16) {
                g_persist_fat_mounted = 1;
                uart_puts("[PERSIST] FAT mounted RW type=");
                {
                    extern void uart_puthex64(uint64_t v);
                    uart_puthex64((uint64_t)(unsigned)mounted);
                }
                uart_puts("\n");
                return;
            }
            uart_puts("[PERSIST] FAT BPB detected but mount failed\n");
            return;
        }
        if (fat == 32) {
            uart_puts("[PERSIST] FAT32 detected (RO mount unsupported; skip BFP1)\n");
            return;
        }
    }
#endif
    if (*(uint32_t *)(g_persist_sec + 0) != BFREE_PERSIST_MAGIC) {
        uart_puts("[PERSIST] blank disk (no BFP1)\n");
        return;
    }
    count = *(uint32_t *)(g_persist_sec + 4);
    if (count > BFREE_GUEST_VFILE_SLOTS) {
        count = BFREE_GUEST_VFILE_SLOTS;
    }
    for (r = 0; r < count; ++r) {
        uint32_t base = 1u + r * BFREE_PERSIST_SECS_PER_REC;
        char name[48];
        uint32_t len;
        int fd;
        bfree_guest_vfile_t *vf;
        uint32_t s;
        size_t off;

        if (bfree_ata_rw_sector(base, g_persist_sec, 0) != 0) {
            return;
        }
        memcpy(name, g_persist_sec, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        len = *(uint32_t *)(g_persist_sec + 48);
        if (len > BFREE_GUEST_VFILE_SIZE ||
            strncmp(name, "persist/", 8) != 0) {
            continue;
        }
        fd = bfree_guest_vfile_alloc_slot(name, 1);
        if (fd < 0) {
            return;
        }
        vf = &g_guest_vfiles[fd - (int)BFREE_GUEST_VFILE_FD_BASE];
        for (s = 0, off = 0; off < len; ++s, off += 512) {
            size_t chunk = len - off;
            if (chunk > 512) {
                chunk = 512;
            }
            if (bfree_ata_rw_sector(base + 1u + s, g_persist_sec, 0) != 0) {
                return;
            }
            memcpy(vf->data + off, g_persist_sec, chunk);
        }
        vf->len = len;
    }
    uart_puts("[PERSIST] loaded from disk\n");
}

/* Call after any mutation of a persist/ vfile. */
static void bfree_persist_maybe_flush(const bfree_guest_vfile_t *vf)
{
    if (vf && strncmp(vf->name, "persist/", 8) == 0) {
        bfree_persist_flush_all();
    }
}
/* ---- end F1 persist ----------------------------------------------------- */

/* Accept /tmp and /tmp/<rel> where <rel> may contain '/' for nested paths.
 * Reject empty components, trailing '/', and "." / ".." segments. */
static int bfree_guest_path_is_under_tmp(const char *path, char *name_out, size_t name_cap)
{
    const char *rel;
    size_t i;
    size_t seg_start;

    if (!path || !name_out || name_cap == 0) {
        return 0;
    }
    if (strcmp(path, "/tmp") == 0) {
        name_out[0] = '\0';
        return 1;
    }
    /* Writable namespaces: /var/* → "var/...", /home/* → "home/...". */
    if (strcmp(path, "/var") == 0) {
        if (name_cap < 4) {
            return 0;
        }
        name_out[0] = 'v'; name_out[1] = 'a'; name_out[2] = 'r'; name_out[3] = '\0';
        return 1;
    }
    if (strcmp(path, "/home") == 0) {
        if (name_cap < 5) {
            return 0;
        }
        name_out[0] = 'h'; name_out[1] = 'o'; name_out[2] = 'm';
        name_out[3] = 'e'; name_out[4] = '\0';
        return 1;
    }
    if (strcmp(path, "/persist") == 0) {
        if (name_cap < 8) {
            return 0;
        }
        bfree_persist_load_once();
        name_out[0]='p'; name_out[1]='e'; name_out[2]='r'; name_out[3]='s';
        name_out[4]='i'; name_out[5]='s'; name_out[6]='t'; name_out[7]='\0';
        return 1;
    }
    if ((strncmp(path, "/var/", 5) == 0 && path[5] != '\0') ||
        (strncmp(path, "/home/", 6) == 0 && path[6] != '\0') ||
        (strncmp(path, "/persist/", 9) == 0 && path[9] != '\0')) {
        const char *src;
        if (path[1] == 'p') {
            bfree_persist_load_once();
        }
        size_t prefix;
        size_t n = 0;
        if (path[1] == 'v') {
            src = path + 1; /* "var/..." */
            prefix = 0;
        } else {
            src = path + 1; /* "home/..." */
            prefix = 0;
        }
        while (src[n] != '\0' && n + 1U < name_cap) {
            name_out[n] = src[n];
            ++n;
        }
        if (src[n] != '\0') {
            return 0;
        }
        name_out[n] = '\0';
        (void)prefix;
        /* Reject . / .. components in the relative part after first slash. */
        {
            const char *rel = name_out;
            size_t i;
            size_t seg_start = 0;
            while (*rel != '/' && *rel != '\0') {
                ++rel;
            }
            if (*rel == '/') {
                ++rel;
            }
            for (i = 0; rel[i] != '\0'; ++i) {
                if (rel[i] == '/') {
                    size_t seglen = i - seg_start;
                    if (seglen == 0) {
                        return 0;
                    }
                    if (seglen == 1 && rel[seg_start] == '.') {
                        return 0;
                    }
                    if (seglen == 2 && rel[seg_start] == '.' &&
                        rel[seg_start + 1] == '.') {
                        return 0;
                    }
                    seg_start = i + 1U;
                }
            }
            if (i == 0 || rel[i - 1] == '/') {
                return 0;
            }
        }
        return 1;
    }
    if (strncmp(path, "/tmp/", 5) != 0) {
        return 0;
    }
    rel = path + 5;
    if (rel[0] == '\0') {
        return 0;
    }
    seg_start = 0;
    for (i = 0; rel[i] != '\0'; ++i) {
        if (rel[i] == '/') {
            size_t seglen = i - seg_start;
            if (seglen == 0) {
                return 0;
            }
            if (seglen == 1 && rel[seg_start] == '.') {
                return 0;
            }
            if (seglen == 2 && rel[seg_start] == '.' && rel[seg_start + 1] == '.') {
                return 0;
            }
            seg_start = i + 1U;
        }
    }
    if (i == 0 || rel[i - 1] == '/') {
        return 0;
    }
    {
        size_t seglen = i - seg_start;
        if (seglen == 0) {
            return 0;
        }
        if (seglen == 1 && rel[seg_start] == '.') {
            return 0;
        }
        if (seglen == 2 && rel[seg_start] == '.' && rel[seg_start + 1] == '.') {
            return 0;
        }
    }
    if (i + 1U > name_cap) {
        return 0;
    }
    memcpy(name_out, rel, i + 1U);
    return 1;
}

/* Return 1 if every parent directory of /tmp/<rel> exists (or rel is top-level). */
static int bfree_guest_tmp_parents_exist(const char *rel)
{
    char parent[48];
    size_t i;
    size_t last_slash = (size_t)-1;

    if (!rel || rel[0] == '\0') {
        return 1;
    }
    for (i = 0; rel[i] != '\0'; ++i) {
        if (rel[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash == (size_t)-1) {
        return 1;
    }
    if (last_slash >= sizeof(parent)) {
        return 0;
    }
    memcpy(parent, rel, last_slash);
    parent[last_slash] = '\0';
    if (strcmp(parent, "var") == 0 || strcmp(parent, "home") == 0 ||
        strcmp(parent, "persist") == 0) {
        return 1;
    }
    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        if (g_guest_vfiles[i].used &&
            !g_guest_vfiles[i].orphaned &&
            g_guest_vfiles[i].is_dir &&
            strcmp(g_guest_vfiles[i].name, parent) == 0) {
            return 1;
        }
    }
    return 0;
}

static int bfree_guest_tmp_has_children(const char *dirname)
{
    size_t i;
    size_t n;

    if (!dirname || dirname[0] == '\0') {
        return 0;
    }
    n = strlen(dirname);
    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        if (!g_guest_vfiles[i].used || g_guest_vfiles[i].orphaned) {
            continue;
        }
        if (strncmp(g_guest_vfiles[i].name, dirname, n) == 0 &&
            g_guest_vfiles[i].name[n] == '/') {
            return 1;
        }
    }
    return 0;
}

/* E: /tmp listing must not show var/home/persist namespace vfiles. */
static int bfree_guest_tmp_root_skip_ns(const char *entry_name)
{
    if (!entry_name) {
        return 1;
    }
    if (strcmp(entry_name, "var") == 0 || strcmp(entry_name, "home") == 0 ||
        strcmp(entry_name, "persist") == 0) {
        return 1;
    }
    if (strncmp(entry_name, "var/", 4) == 0 ||
        strncmp(entry_name, "home/", 5) == 0 ||
        strncmp(entry_name, "persist/", 8) == 0) {
        return 1;
    }
    return 0;
}

/* Emit the basename of an immediate child of parent_rel ("" for /tmp). */
static int bfree_guest_tmp_child_basename(const char *entry_name,
                                          const char *parent_rel,
                                          char *base_out,
                                          size_t base_cap)
{
    size_t plen;
    const char *rest;
    size_t i;

    if (!entry_name || !base_out || base_cap == 0) {
        return 0;
    }
    plen = parent_rel ? strlen(parent_rel) : 0;
    if (plen == 0) {
        rest = entry_name;
    } else {
        if (strncmp(entry_name, parent_rel, plen) != 0 || entry_name[plen] != '/') {
            return 0;
        }
        rest = entry_name + plen + 1U;
    }
    if (rest[0] == '\0') {
        return 0;
    }
    for (i = 0; rest[i] != '\0'; ++i) {
        if (rest[i] == '/') {
            return 0; /* deeper than immediate child */
        }
    }
    if (i + 1U > base_cap) {
        return 0;
    }
    memcpy(base_out, rest, i + 1U);
    return 1;
}

/* Rewrite a relative path in place as <cwd>/<path> so applets run after
 * chdir (e.g. `tar -C /tmp b2f`) resolve names the same way absolute paths
 * do. "." and "./" collapse to the cwd itself. */
static void bfree_guest_path_absolutize(char *path, size_t cap)
{
    char out[256];
    const char *rel = path;
    size_t n = 0;
    size_t i;

    if (path[0] == '/' || cap == 0) {
        return;
    }
    if (rel[0] == '.' && rel[1] == '\0') {
        rel = "";
    } else if (rel[0] == '.' && rel[1] == '/') {
        rel += 2;
    }
    for (i = 0; g_guest_cwd[i] != '\0' && n + 1U < sizeof(out); ++i) {
        out[n++] = g_guest_cwd[i];
    }
    if (rel[0] != '\0') {
        if (n == 0 || out[n - 1U] != '/') {
            if (n + 1U < sizeof(out)) {
                out[n++] = '/';
            }
        }
        for (i = 0; rel[i] != '\0' && n + 1U < sizeof(out); ++i) {
            out[n++] = rel[i];
        }
    }
    if (n == 0) {
        out[n++] = '/';
    }
    out[n] = '\0';
    for (i = 0; out[i] != '\0' && i + 1U < cap; ++i) {
        path[i] = out[i];
    }
    path[i] = '\0';
}

#define BFREE_LINUX_AT_FDCWD (-100)

/* Resolve path relative to dirfd (or AT_FDCWD) into an absolute path in-place.
 * Absolute paths are left unchanged. Returns 0 or a negated errno. */
static long bfree_guest_path_at(long dirfd, char *path, size_t cap)
{
    char base[256];
    char out[256];
    const char *rel;
    size_t n = 0;
    size_t i;
    int resolved;
    int target;
    bfree_guest_ofd_t *ofd;
    bfree_guest_vfile_t *vf;

    if (!path || cap == 0) {
        return -22;
    }
    if (path[0] == '/') {
        return 0;
    }

    if (dirfd == (long)BFREE_LINUX_AT_FDCWD) {
        for (i = 0; g_guest_cwd[i] != '\0' && i + 1U < sizeof(base); ++i) {
            base[i] = g_guest_cwd[i];
        }
        base[i] = '\0';
        if (i == 0) {
            base[0] = '/';
            base[1] = '\0';
        }
    } else {
        if (dirfd < 0) {
            return -9; /* EBADF */
        }
        resolved = bfree_guest_fd_resolve((int)dirfd);
        ofd = bfree_guest_ofd_from_fd(resolved);
        target = ofd ? ofd->target : resolved;
        vf = bfree_guest_vfile_from_fd(target);

        if (target == (int)BFREE_GUEST_ROOT_DIR_FD) {
            base[0] = '/';
            base[1] = '\0';
        } else if (target == (int)BFREE_GUEST_TMP_DIR_FD) {
            base[0] = '/';
            base[1] = 't';
            base[2] = 'm';
            base[3] = 'p';
            base[4] = '\0';
        } else if (target == (int)BFREE_GUEST_BIN_DIR_FD) {
            base[0] = '/';
            base[1] = 'b';
            base[2] = 'i';
            base[3] = 'n';
            base[4] = '\0';
        } else if (target == (int)BFREE_GUEST_USR_DIR_FD) {
            base[0] = '/';
            base[1] = 'u';
            base[2] = 's';
            base[3] = 'r';
            base[4] = '\0';
        } else if (target == (int)BFREE_GUEST_VAR_DIR_FD) {
            base[0] = '/';
            base[1] = 'v';
            base[2] = 'a';
            base[3] = 'r';
            base[4] = '\0';
        } else if (target == (int)BFREE_GUEST_HOME_DIR_FD) {
            base[0] = '/';
            base[1] = 'h';
            base[2] = 'o';
            base[3] = 'm';
            base[4] = 'e';
            base[5] = '\0';
        } else if (target == (int)BFREE_GUEST_PERSIST_DIR_FD) {
            base[0] = '/';
            base[1] = 'p';
            base[2] = 'e';
            base[3] = 'r';
            base[4] = 's';
            base[5] = 'i';
            base[6] = 's';
            base[7] = 't';
            base[8] = '\0';
        } else if (target == (int)BFREE_GUEST_PROC_DIR_FD) {
            base[0] = '/';
            base[1] = 'p';
            base[2] = 'r';
            base[3] = 'o';
            base[4] = 'c';
            base[5] = '\0';
        } else if (vf && vf->is_dir && !vf->orphaned && vf->name[0] != '\0') {
            size_t bn = 0;

            base[bn++] = '/';
            base[bn++] = 't';
            base[bn++] = 'm';
            base[bn++] = 'p';
            base[bn++] = '/';
            for (i = 0; vf->name[i] != '\0' && bn + 1U < sizeof(base); ++i) {
                base[bn++] = vf->name[i];
            }
            base[bn] = '\0';
        } else {
            return -20; /* ENOTDIR */
        }
    }

    rel = path;
    if (rel[0] == '.' && rel[1] == '\0') {
        rel = "";
    } else if (rel[0] == '.' && rel[1] == '/') {
        rel += 2;
    }
    for (i = 0; base[i] != '\0' && n + 1U < sizeof(out); ++i) {
        out[n++] = base[i];
    }
    if (rel[0] != '\0') {
        if (n == 0 || out[n - 1U] != '/') {
            if (n + 1U < sizeof(out)) {
                out[n++] = '/';
            }
        }
        for (i = 0; rel[i] != '\0' && n + 1U < sizeof(out); ++i) {
            out[n++] = rel[i];
        }
    }
    if (n == 0) {
        out[n++] = '/';
    }
    out[n] = '\0';
    for (i = 0; out[i] != '\0' && i + 1U < cap; ++i) {
        path[i] = out[i];
    }
    path[i] = '\0';
    return 0;
}

static long sys_linux_dup(long oldfd)
{
    int i;
    int resolved;

    if (oldfd < 0) {
        return -9;
    }
    resolved = bfree_guest_fd_resolve((int)oldfd);
    if (resolved < 0) {
        return -9;
    }
    for (i = 3; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        if (g_guest_fd_target[i] < 0 && g_guest_fd_dup_save[i] < 0) {
            g_guest_fd_target[i] = resolved;
            g_guest_fd_dup_save[i] = resolved;
            if (bfree_guest_is_pipe_wr(resolved) || bfree_guest_is_pipe_rd(resolved)) {
                bfree_guest_pipe_ref(resolved, 1);
            }
            return i;
        }
    }
    return -24;
}

static long sys_linux_dup2(long oldfd, long newfd)
{
    int resolved;

    if (newfd < 0 || newfd >= BFREE_GUEST_FD_TABLE_SIZE) {
        return -22;
    }
    if (oldfd < 0) {
        return -9;
    }
    if (oldfd == newfd) {
        return newfd;
    }
    resolved = bfree_guest_fd_dup2_resolve_old((int)oldfd);
    if (resolved < 0) {
        return -9;
    }
    bfree_guest_fd_apply_dup2((int)oldfd, (int)newfd, resolved);
    return newfd;
}

static const char g_guest_busybox_exe_path[] = "/busybox.elf";
static size_t g_guest_busybox_off;

static size_t g_guest_memfd_size;
static unsigned g_guest_memfd_seq;

static long sys_linux_memfd_create(long name_ptr, long flags)
{
    char vname[48];
    const char *tag = "mfd";
    size_t i = 0;
    int target;
    int pub;
    int cloexec;
    bfree_guest_vfile_t *dir;
    unsigned seq;

    (void)name_ptr;
    cloexec = ((unsigned long)flags & (unsigned long)BFREE_MFD_CLOEXEC) != 0UL;
    seq = g_guest_memfd_seq++;
    /* vfile name: memfd/<seq> under /tmp namespace */
    vname[0] = 'm';
    vname[1] = 'e';
    vname[2] = 'm';
    vname[3] = 'f';
    vname[4] = 'd';
    vname[5] = '/';
    {
        char num[16];
        size_t n = 0;
        unsigned v = seq;

        if (v == 0U) {
            num[n++] = '0';
        } else {
            while (v > 0U && n < sizeof(num)) {
                num[n++] = (char)('0' + (v % 10U));
                v /= 10U;
            }
        }
        while (n > 0U && i + 7U < sizeof(vname)) {
            vname[6 + i] = num[--n];
            ++i;
        }
    }
    vname[6 + i] = '\0';
    (void)tag;

    dir = bfree_guest_vfile_find_by_name("memfd");
    if (!dir) {
        int dfd = bfree_guest_vfile_alloc_slot("memfd", 1);
        if (dfd < 0) {
            return dfd;
        }
        dir = bfree_guest_vfile_from_fd(dfd);
        if (dir) {
            dir->is_dir = 1;
        }
    }
    target = bfree_guest_vfile_alloc_slot(vname, 1);
    if (target < 0) {
        return target;
    }
    g_guest_memfd_size = 0;
    pub = bfree_guest_vfile_publish_open(target, 2 /* O_RDWR */, 0);
    if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_cloexec[pub] = 1;
    }
    return pub;
}

/* memfd vnodes live under the synthetic "memfd/" directory. */
static int bfree_guest_vfile_is_memfd(const bfree_guest_vfile_t *vf)
{
    return vf != 0 && strncmp(vf->name, "memfd/", 6) == 0;
}

static long sys_linux_ftruncate(long fd, long length)
{
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;

    if (length < 0) {
        return -22;
    }
    if (fd == (long)BFREE_GUEST_MEMFD_FD) {
        g_guest_memfd_size = (size_t)(unsigned long)length;
        return 0;
    }
    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (vf) {
        size_t new_len = (size_t)(unsigned long)length;

        if (new_len > BFREE_GUEST_VFILE_SIZE) {
            return -28;
        }
        if (new_len > vf->len &&
            (vf->seals & (uint32_t)BFREE_F_SEAL_GROW) != 0U) {
            return -1; /* EPERM */
        }
        if (new_len < vf->len &&
            (vf->seals & (uint32_t)BFREE_F_SEAL_SHRINK) != 0U) {
            return -1; /* EPERM */
        }
        if (new_len > vf->len) {
            size_t i;

            for (i = vf->len; i < new_len; ++i) {
                vf->data[i] = 0;
            }
        }
        vf->len = new_len;
        if (ofd && ofd->pos > vf->len) {
            ofd->pos = vf->len;
        } else if (!ofd && vf->pos > vf->len) {
            vf->pos = vf->len;
        }
        bfree_persist_maybe_flush(vf);
        return 0;
    }
    return 0;
}

static long sys_linux_truncate(long path_ptr, long length)
{
    char path[192];
    char vname[64];
    bfree_guest_vfile_t *vf;
    long path_err;
    size_t new_len;

    if (length < 0) {
        return -22;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(BFREE_LINUX_AT_FDCWD, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) || vname[0] == '\0') {
        return -30; /* EROFS outside /tmp */
    }
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf || vf->orphaned) {
        return -2;
    }
    if (vf->is_dir) {
        return -21;
    }
    if (vf->is_symlink) {
        return -22;
    }
    new_len = (size_t)(unsigned long)length;
    if (new_len > BFREE_GUEST_VFILE_SIZE) {
        return -28;
    }
    if (new_len > vf->len) {
        size_t i;

        for (i = vf->len; i < new_len; ++i) {
            vf->data[i] = 0;
        }
    }
    vf->len = new_len;
    if (vf->pos > vf->len) {
        vf->pos = vf->len;
    }
    bfree_persist_maybe_flush(vf);
    return 0;
}

static unsigned g_guest_umask = 022;

static long sys_linux_umask(long mask)
{
    unsigned old = g_guest_umask;

    g_guest_umask = (unsigned)mask & 0777U;
    return (long)old;
}

static const char g_guest_proc_maps_desktop[] =
    /* QV4 stackProperties() parses the first region containing stackAddr. */
    "08000000-10000000 rw-p 00000000 00:00 0                  [stack]\n"
    "00100000-02000000 rw-p 00000000 00:00 0                  [stack]\n"
    "08000000-14000000 r-xp 00000000 00:00 0                  /desktop\n"
    "19000000-21000000 rw-p 00000000 00:00 0                  [heap]\n";
static const char g_guest_proc_maps_busybox[] =
    "05000000-05380000 r-xp 00000000 00:00 0                  /busybox.elf\n"
    "05380000-05400000 rw-p 00000000 00:00 0                  /busybox.elf\n"
    "03c00000-2a000000 rw-p 00000000 00:00 0                  [heap]\n"
    "01380000-01400000 rw-p 00000000 00:00 0                  [stack]\n";
static const char *g_guest_proc_maps = g_guest_proc_maps_desktop;
static size_t g_guest_proc_maps_off;

/* Minimal /proc content for BusyBox ps / free / uptime. */
static const char g_guest_proc_pid_stat[] =
    "1 (busybox) S 0 1 1 0 -1 4194560 0 0 0 0 10 5 0 0 20 0 1 0 123 139264 348 "
    "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
static size_t g_guest_proc_pid_stat_off;
static const char g_guest_proc_cmdline[] = "sh\0";
static size_t g_guest_proc_cmdline_off;
static const char g_guest_proc_meminfo[] =
    "MemTotal:         524288 kB\n"
    "MemFree:          393216 kB\n"
    "MemAvailable:     400000 kB\n"
    "Buffers:            1024 kB\n"
    "Cached:            16384 kB\n"
    "SwapTotal:             0 kB\n"
    "SwapFree:              0 kB\n"
    "SReclaimable:        512 kB\n";
static size_t g_guest_proc_meminfo_off;
static const char g_guest_proc_uptime_file[] = "120.00 100.00\n";
static size_t g_guest_proc_uptime_off;
static const char g_guest_proc_loadavg[] = "0.00 0.00 0.00 2/2 2\n";
static size_t g_guest_proc_loadavg_off;
static const char g_guest_proc_cpustat[] =
    "cpu  10 0 10 1000 0 0 0 0 0 0\n"
    "cpu0 10 0 10 1000 0 0 0 0 0 0\n";
static size_t g_guest_proc_cpustat_off;
static const char g_guest_proc_status[] =
    "Name:\tbusybox\n"
    "State:\tS (sleeping)\n"
    "Pid:\t1\n"
    "PPid:\t0\n"
    "Uid:\t0\t0\t0\t0\n"
    "Gid:\t0\t0\t0\t0\n"
    "VmSize:\t   136 kB\n"
    "VmRSS:\t     1 kB\n"
    "Threads:\t1\n";
static size_t g_guest_proc_status_off;
static const char g_guest_proc_mounts_base[] =
    "rootfs / rootfs rw 0 0\n"
    "proc /proc proc rw,relatime 0 0\n"
    "tmpfs /tmp tmpfs rw,relatime 0 0\n"
    "vfile /persist vfile rw,relatime 0 0\n"
    "vfile /home vfile rw,relatime 0 0\n"
    "vfile /var vfile rw,relatime 0 0\n";
#define BFREE_GUEST_MOUNT_SLOTS 8
#define BFREE_PROC_MOUNTS_CAP 1536
static char g_guest_proc_mounts_buf[BFREE_PROC_MOUNTS_CAP];
static size_t g_guest_proc_mounts_len;
static size_t g_guest_proc_mounts_off;
static int g_guest_proc_mounts_ready;
static struct {
    int used;
    char source[48];
    char target[96];
    char fstype[24];
} g_guest_dyn_mounts[BFREE_GUEST_MOUNT_SLOTS];

static void bfree_guest_proc_mounts_append(char *dst, size_t *npos, size_t cap,
                                           const char *s)
{
    size_t i = *npos;

    if (!s) {
        return;
    }
    while (*s != '\0' && i + 1U < cap) {
        dst[i++] = *s++;
    }
    *npos = i;
}

static void bfree_guest_proc_mounts_rebuild(void)
{
    size_t n = 0;
    int i;

    bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                   BFREE_PROC_MOUNTS_CAP, g_guest_proc_mounts_base);
    for (i = 0; i < BFREE_GUEST_MOUNT_SLOTS; ++i) {
        if (!g_guest_dyn_mounts[i].used) {
            continue;
        }
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP,
                                       g_guest_dyn_mounts[i].source[0]
                                           ? g_guest_dyn_mounts[i].source
                                           : "none");
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP, " ");
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP,
                                       g_guest_dyn_mounts[i].target);
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP, " ");
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP,
                                       g_guest_dyn_mounts[i].fstype[0]
                                           ? g_guest_dyn_mounts[i].fstype
                                           : "none");
        bfree_guest_proc_mounts_append(g_guest_proc_mounts_buf, &n,
                                       BFREE_PROC_MOUNTS_CAP, " rw,relatime 0 0\n");
    }
    if (n >= BFREE_PROC_MOUNTS_CAP) {
        n = BFREE_PROC_MOUNTS_CAP - 1U;
    }
    g_guest_proc_mounts_buf[n] = '\0';
    g_guest_proc_mounts_len = n;
    g_guest_proc_mounts_ready = 1;
}

static void bfree_guest_proc_mounts_ensure(void)
{
    if (!g_guest_proc_mounts_ready) {
        bfree_guest_proc_mounts_rebuild();
    }
}
/* Synthetic PID 2 so ps shows more than one task without a live fork child. */
static const char g_guest_proc_pid2_stat[] =
    "2 (kworker) S 0 2 2 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 1 0 0 "
    "18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
static size_t g_guest_proc_pid2_stat_off;
static const char g_guest_proc_pid2_cmdline[] = "\0";
static size_t g_guest_proc_pid2_cmdline_off;

static void bfree_guest_proc_maps_select_busybox(int is_busybox)
{
    g_guest_proc_maps = is_busybox ? g_guest_proc_maps_busybox : g_guest_proc_maps_desktop;
    g_guest_proc_maps_off = 0;
}

static size_t bfree_guest_cstr_len(const char *s)
{
    size_t n = 0;

    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static long bfree_guest_read_blob(long buf, long count, const char *src, size_t total, size_t *offp)
{
    uint8_t *dst;
    size_t off;
    size_t n;
    size_t i;

    if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    off = *offp;
    if (off >= total) {
        return 0;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    n = (size_t)count;
    if (n > total - off) {
        n = total - off;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = (uint8_t)src[off + i];
    }
    *offp = off + n;
    return (long)n;
}

static const char g_guest_etc_passwd[] = "root:x:0:0:root:/root:/bin/sh\n";
static size_t g_guest_etc_passwd_off;

static const char g_guest_etc_group[] = "root:x:0:\n";
static size_t g_guest_etc_group_off;

static const char g_guest_etc_profile[] =
    "# B-Free guest profile\n"
    "export PATH=/bin:/usr/bin:.\n"
    "export PS1='root@bfree:# '\n";
static size_t g_guest_etc_profile_off;

static const char g_guest_etc_motd[] = "";
static size_t g_guest_etc_motd_off;

/* H32: musl getaddrinfo reads /etc/hosts before DNS; QEMU slirp-style guest IPs. */
static const char g_guest_etc_hosts[] =
    "127.0.0.1\tlocalhost\n"
    "::1\tlocalhost\n"
    "10.0.2.15\tbfree guest\n"
    "10.0.2.2\tgateway\n";
static size_t g_guest_etc_hosts_off;

/* Resolver config — UDP DNS to 10.0.2.3 is stubbed from /etc/hosts (B). */
static const char g_guest_etc_resolv[] =
    "nameserver 10.0.2.3\n"
    "search local\n";
static size_t g_guest_etc_resolv_off;

static void bfree_guest_exec_reset_subsystems(int is_busybox)
{
    int i;

    g_guest_cwd[0] = '/';
    g_guest_cwd[1] = '\0';
    if (knl_current_task != 0 && knl_current_task->page_table_base != 0) {
        page_table_t *pt = (page_table_t *)knl_current_task->page_table_base;
        uint64_t hi = g_guest_heap_next;

        if (hi > (uint64_t)BFREE_GUEST_HEAP_BASE) {
            bfree_guest_heap_unmap_range(pt, (uint64_t)BFREE_GUEST_HEAP_BASE, hi);
        }
    }
    bfree_guest_heap_reset();
    g_guest_tty_termios_inited = 0;
    g_guest_proc_maps_off = 0;
    g_guest_proc_pid_stat_off = 0;
    g_guest_proc_cmdline_off = 0;
    g_guest_proc_meminfo_off = 0;
    g_guest_proc_uptime_off = 0;
    g_guest_proc_loadavg_off = 0;
    g_guest_proc_cpustat_off = 0;
    g_guest_proc_status_off = 0;
    g_guest_proc_mounts_off = 0;
    g_guest_proc_pid2_stat_off = 0;
    g_guest_proc_pid2_cmdline_off = 0;
    g_guest_etc_passwd_off = 0;
    g_guest_etc_group_off = 0;
    g_guest_etc_profile_off = 0;
    g_guest_etc_motd_off = 0;
    g_guest_etc_hosts_off = 0;
    g_guest_etc_resolv_off = 0;
    bfree_guest_proc_maps_select_busybox(is_busybox);
    bfree_guest_pipe_reset_all();
    bfree_guest_fd_ensure_init();
    /* Drop stale fork/coop only — full process_init here races AS-copy
     * fork PT readiness and faulted the first applet (wc) on heap PTEs. */
    g_guest_fork_active = 0;
    g_guest_fork_pid = 0;
    g_guest_fork_status_ready = 0;
    g_guest_fork_was_as_copy = 0;
    g_coop_side = 0;
    g_coop_child_blocked = 0;
    g_coop_parent_started = 0;
    g_guest_waitid_active = 0;
    g_guest_waitid_infop = 0;
    g_guest_wait_status_ptr = 0;
    bfree_guest_thread_init();
    g_guest_eventfd_next = 0;
    for (i = 0; i < BFREE_MAX_GUEST_EVENTFD; ++i) {
        g_guest_eventfd_val[i] = 0;
        g_guest_eventfd_nonblock[i] = 0;
        g_guest_eventfd_sem[i] = 0;
    }
    g_guest_pgid = 1;
    g_guest_tty_pgrp = 1;
    g_guest_sid = 1;
    for (i = 3; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        g_guest_fd_target[i] = -1;
        g_guest_fd_dup_save[i] = -1;
    }
    for (i = 0; i < BFREE_GUEST_OFD_SLOTS; ++i) {
        memset(&g_guest_ofds[i], 0, sizeof(g_guest_ofds[i]));
    }
}

/* vfork+execve applet: fresh heap, keep pipeline pipes and stdin/stdout dup state. */
static void bfree_guest_execve_reset_subsystems(int is_busybox)
{
    if (knl_current_task != 0 && knl_current_task->page_table_base != 0) {
        page_table_t *pt = (page_table_t *)knl_current_task->page_table_base;
        uint64_t hi = g_guest_heap_next;

        if (hi > (uint64_t)BFREE_GUEST_HEAP_BASE) {
            bfree_guest_heap_unmap_range(pt, (uint64_t)BFREE_GUEST_HEAP_BASE, hi);
        }
    }
    bfree_guest_heap_reset();
    g_guest_proc_maps_off = 0;
    g_guest_proc_pid_stat_off = 0;
    g_guest_proc_cmdline_off = 0;
    g_guest_proc_meminfo_off = 0;
    g_guest_proc_uptime_off = 0;
    g_guest_proc_loadavg_off = 0;
    g_guest_proc_cpustat_off = 0;
    g_guest_proc_status_off = 0;
    g_guest_proc_mounts_off = 0;
    g_guest_proc_pid2_stat_off = 0;
    g_guest_proc_pid2_cmdline_off = 0;
    g_guest_etc_passwd_off = 0;
    g_guest_etc_group_off = 0;
    g_guest_etc_profile_off = 0;
    g_guest_etc_motd_off = 0;
    g_guest_etc_hosts_off = 0;
    g_guest_etc_resolv_off = 0;
    bfree_guest_proc_maps_select_busybox(is_busybox);
}

static long sys_linux_getpid(void)
{
    if (g_guest_fork_active) {
        return (long)g_guest_fork_pid;
    }
    return 1;
}

static long sys_linux_sysinfo(long info_ptr)
{
    /* Linux x86_64 struct sysinfo (see linux/sysinfo.h). */
    typedef struct {
        int64_t uptime;
        uint64_t loads[3];
        uint64_t totalram;
        uint64_t freeram;
        uint64_t sharedram;
        uint64_t bufferram;
        uint64_t totalswap;
        uint64_t freeswap;
        uint16_t procs;
        uint16_t pad;
        uint64_t totalhigh;
        uint64_t freehigh;
        uint32_t mem_unit;
        char _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
    } bfree_linux_sysinfo_t;
    bfree_linux_sysinfo_t info;
    uint8_t *dst;
    size_t i;

    if (info_ptr == 0 || !bfree_user_ptr_mapped(info_ptr)) {
        return -14;
    }
    memset(&info, 0, sizeof(info));
    info.uptime = (int64_t)(knl_get_current_time() / 1000000ULL);
    if (info.uptime < 1) {
        info.uptime = 1;
    }
    info.totalram = 512ULL * 1024ULL * 1024ULL;
    info.freeram = 384ULL * 1024ULL * 1024ULL;
    info.bufferram = 1024ULL * 1024ULL;
    info.procs = 2;
    info.mem_unit = 1U;
    dst = (uint8_t *)(uintptr_t)info_ptr;
    for (i = 0; i < sizeof(info); ++i) {
        dst[i] = ((const uint8_t *)&info)[i];
    }
    return 0;
}

static long sys_linux_getppid(void)
{
    /* Guest fork model: children always report ppid=1 while the coop session
     * is live (matches getpid parent = 1). Side-gating broke GETLK checks when
     * g_coop_side briefly disagreed with the running child. */
    if (g_guest_fork_active) {
        return 1;
    }
    return 0;
}

static long sys_linux_gettid(void)
{
    if (g_guest_thread_active) {
        return (long)g_guest_thread_tid;
    }
    if (g_guest_fork_active) {
        return (long)g_guest_fork_pid;
    }
    return 1;
}

static int bfree_guest_sig_is_fatal_default(int sig)
{
    switch (sig) {
    case 1: case 2: case 3: case 6: case 9: case 15:
        return 1;
    default:
        return 0;
    }
}

static long sys_linux_kill(long pid, long sig)
{
    int s = (int)sig;
    int target = (int)pid;
    int self_pid = g_guest_fork_active ? g_guest_fork_pid : 1;

    if (s < 0 || s >= BFREE_NSIG) {
        return -22;
    }
    if (target < -1) {
        int pg = (int)(-target);
        if (g_guest_fork_active && bfree_process_getpgid(g_guest_fork_pid) == pg) {
            target = g_guest_fork_pid;
        } else if (pg == g_guest_pgid || pg == g_guest_tty_pgrp) {
            target = self_pid;
        } else if (!bfree_process_pgid_has_member(pg)) {
            return -3;
        } else {
            target = self_pid;
        }
    }
    if (target == -1 || target == 0) {
        target = self_pid;
    }
    if (s == 0) {
        if (target == 1 || target == 2 || target == self_pid ||
            (g_guest_fork_active && target == g_guest_fork_pid) ||
            bfree_process_getpgid(target) > 0) {
            return 0;
        }
        return -3;
    }
    if (s == BFREE_SIGCONT || s == 18) {
        (void)bfree_process_cont_pid(
            (target == g_guest_fork_pid || target == self_pid)
                ? (g_guest_fork_pid > 0 ? g_guest_fork_pid : target)
                : target);
        bfree_guest_sig_raise(s);
        return 0;
    }
    if ((s == BFREE_SIGTSTP || s == BFREE_SIGSTOP || s == 19 || s == 20) &&
        g_guest_fork_active && g_coop_side == 1 &&
        (target == self_pid || target == g_guest_fork_pid)) {
        return bfree_guest_stop_from_fork(s);
    }
    /* Fatal signal to live coop child (incl. kill -TERM $$ inside the child). */
    if (g_guest_fork_active &&
        (target == g_guest_fork_pid || target == self_pid) &&
        (bfree_guest_sig_is_fatal_default(s) || s == 9)) {
        if (s != 9 && g_guest_sig_disp[s] == BFREE_SIG_IGN) {
            return 0;
        }
        bfree_guest_fork_child_pipe_close_writers();
        return bfree_guest_exit_from_fork_signal(s);
    }
    if (target == 1 || target == 2 || target == self_pid ||
        (g_guest_fork_status_ready && target == g_guest_fork_pid)) {
        bfree_guest_sig_raise(s);
        return 0;
    }
    return -3; /* ESRCH */
}

/* Mount kinds reported by statfs/fstatfs. */
#define BFREE_STATFS_ROOT  0
#define BFREE_STATFS_TMPFS 1
#define BFREE_STATFS_PROC  2

static long bfree_statfs_emit(int kind, long buf)
{
    typedef struct {
        int64_t f_type;
        int64_t f_bsize;
        uint64_t f_blocks;
        uint64_t f_bfree;
        uint64_t f_bavail;
        uint64_t f_files;
        uint64_t f_ffree;
        struct { int32_t val[2]; } f_fsid;
        int64_t f_namelen;
        int64_t f_frsize;
        int64_t f_flags;
        int64_t f_spare[4];
    } bfree_statfs_t;
    bfree_statfs_t st;
    uint8_t *dst;
    size_t i;

    memset(&st, 0, sizeof(st));
    st.f_bsize = 4096;
    st.f_namelen = 255;
    st.f_frsize = 4096;
    if (kind == BFREE_STATFS_TMPFS) {
        st.f_type = 0x01021994; /* TMPFS_MAGIC */
        st.f_blocks = 65536;    /* 256MB tmpfs */
        st.f_bfree = 61440;
        st.f_bavail = 61440;
        st.f_files = BFREE_GUEST_VFILE_SLOTS;
        st.f_ffree = BFREE_GUEST_VFILE_SLOTS - 4;
    } else if (kind == BFREE_STATFS_PROC) {
        st.f_type = 0x9fa0;     /* PROC_SUPER_MAGIC */
        st.f_blocks = 0;
        st.f_bfree = 0;
        st.f_bavail = 0;
        st.f_files = 0;
        st.f_ffree = 0;
    } else {
        st.f_type = 0x01021994; /* synthetic rootfs (tmpfs-backed) */
        st.f_blocks = 131072;   /* 512MB */
        st.f_bfree = 98304;
        st.f_bavail = 98304;
        st.f_files = 1024;
        st.f_ffree = 1000;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < sizeof(st); ++i) {
        dst[i] = ((const uint8_t *)&st)[i];
    }
    return 0;
}

static long sys_linux_statfs(long path_ptr, long buf)
{
    char path[256];
    int kind = BFREE_STATFS_ROOT;

    if (buf == 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    path[0] = '\0';
    if (path_ptr != 0 && copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    if (strncmp(path, "/tmp", 4) == 0 && (path[4] == '\0' || path[4] == '/')) {
        kind = BFREE_STATFS_TMPFS;
    } else if (strncmp(path, "/dev/shm", 8) == 0) {
        kind = BFREE_STATFS_TMPFS;
    } else if (strncmp(path, "/proc", 5) == 0 &&
               (path[5] == '\0' || path[5] == '/')) {
        kind = BFREE_STATFS_PROC;
    }
    return bfree_statfs_emit(kind, buf);
}

static long sys_linux_fstatfs(long fd, long buf)
{
    int resolved;

    if (fd < 0) {
        return -9; /* EBADF */
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved < 0) {
        return -9;
    }
    /* Small fds above stdio must have been published to be open. */
    if (fd > 2 && fd < BFREE_GUEST_FD_TABLE_SIZE &&
        g_guest_fd_target[fd] < 0 && g_guest_fd_dup_save[fd] < 0) {
        return -9; /* EBADF */
    }
    if (buf == 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    /* vfiles (incl. memfd / shm) all live on the synthetic tmpfs. */
    if (bfree_guest_vfile_from_open_fd(resolved, 0)) {
        return bfree_statfs_emit(BFREE_STATFS_TMPFS, buf);
    }
    return bfree_statfs_emit(BFREE_STATFS_ROOT, buf);
}

static long sys_linux_prlimit64(long pid, long resource, long new_limit, long old_limit)
{
    typedef struct {
        uint64_t rlim_cur;
        uint64_t rlim_max;
    } bfree_rlimit64_t;
    bfree_rlimit64_t soft;
    bfree_rlimit64_t neu;
    uint8_t *dst;
    const uint8_t *src;
    size_t i;

    (void)pid;
    bfree_rlimit_defaults(resource, &soft.rlim_cur, &soft.rlim_max);
    if (old_limit != 0) {
        if (!bfree_user_ptr_mapped(old_limit)) {
            return -14;
        }
        dst = (uint8_t *)(uintptr_t)old_limit;
        src = (const uint8_t *)&soft;
        for (i = 0; i < sizeof(soft); ++i) {
            dst[i] = src[i];
        }
    }
    if (new_limit != 0) {
        if (!bfree_user_ptr_mapped(new_limit)) {
            return -14;
        }
        neu = *(const bfree_rlimit64_t *)(uintptr_t)new_limit;
        if (neu.rlim_cur > neu.rlim_max) {
            return -22;
        }
        bfree_rlimit_store(resource, neu.rlim_cur, neu.rlim_max);
    }
    return 0;
}

static int bfree_stdin_byte_ready(void);
static int bfree_stdin_pop_byte(uint8_t *out_ch);
static long bfree_stdin_read_user(long buf, long count);

static long sys_linux_read(long fd, long buf, long count)
{
    uint8_t *dst;
    size_t n;
    size_t i;
    size_t *posp;
    int orig_fd = (int)fd;
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;

    fd = bfree_guest_fd_resolve((int)fd);
    if ((int)fd >= BFREE_INOTIFY_FD_BASE &&
        (int)fd < BFREE_INOTIFY_FD_BASE + BFREE_MAX_INOTIFY) {
        return bfree_inotify_read((int)fd, buf, count);
    }
    {
        int iidx = bfree_inet_from_fd((int)fd);
        if (iidx >= 0 && g_inet_socks[iidx].shut_rd) {
            return 0; /* SHUT_RD: further reads are EOF */
        }
        if (iidx >= 0 && g_inet_socks[iidx].tcp_pcb >= 0) {
            int pcb = g_inet_socks[iidx].tcp_pcb;
            int got;
            int spins;
            if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
                return -14;
            }
            for (spins = 0; spins < 64; ++spins) {
                int st = tcp_min_pump(pcb);
                if (st == 0) {
                    g_inet_socks[iidx].connected = 1;
                    break;
                }
                if (st != -115) {
                    return (long)st;
                }
                net_runtime_poll();
            }
            if (!g_inet_socks[iidx].connected) {
                return -115;
            }
            for (spins = 0; spins < 64; ++spins) {
                got = tcp_min_recv(pcb, (uint8_t *)(uintptr_t)buf, (size_t)count);
                if (got != -11) {
                    return (long)got;
                }
                net_runtime_poll();
            }
            return -11;
        }
        /* Loopback TCP stream: read from accept_rd (peer write pipe). */
        if (iidx >= 0 && g_inet_socks[iidx].connected) {
            int mag = g_inet_socks[iidx].accept_rd;
            if (mag < 0 && g_inet_socks[iidx].pipe_magic >= 0) {
                mag = g_inet_socks[iidx].pipe_magic;
                if (bfree_guest_pipe_is_wr_magic(mag)) {
                    mag = mag - 1;
                }
            }
            if (mag >= 0) {
                return sys_linux_read(mag, buf, count);
            }
        }
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);
        if (uidx >= 0 && g_unix_socks[uidx].shut_rd) {
            return 0;
        }
        if (uidx >= 0 && g_unix_socks[uidx].is_dgram) {
            return bfree_unix_dgram_recv(uidx, buf, count, 0, 0, 0);
        }
        if (uidx >= 0 && g_unix_socks[uidx].connected) {
            int mag = g_unix_socks[uidx].accept_rd;
            if (mag < 0 && g_unix_socks[uidx].pipe_magic >= 0) {
                mag = g_unix_socks[uidx].pipe_magic;
                if (bfree_guest_pipe_is_wr_magic(mag)) {
                    mag = mag - 1;
                }
            }
            if (mag >= 0) {
                return sys_linux_read(mag, buf, count);
            }
        }
    }
    if (fd == 0) {
        return bfree_stdin_read_user(buf, count);
    }
    if (fd == (long)BFREE_GUEST_DEV_NULL_FD) {
        return 0;
    }
    if (fd == (long)BFREE_GUEST_DEV_URANDOM_FD) {
        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        for (i = 0; i < n; ++i) {
            dst[i] = (uint8_t)(0x5A ^ (uint8_t)i);
        }
        return (long)n;
    }
    if (fd == (long)BFREE_GUEST_PROC_MAPS_FD) {
        size_t total = bfree_guest_cstr_len(g_guest_proc_maps);

        return bfree_guest_read_blob(buf, count, g_guest_proc_maps, total, &g_guest_proc_maps_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_PIDSTAT_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_pid_stat,
            sizeof(g_guest_proc_pid_stat) - 1U, &g_guest_proc_pid_stat_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_CMDLINE_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_cmdline,
            sizeof(g_guest_proc_cmdline) - 1U, &g_guest_proc_cmdline_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_MEMINFO_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_meminfo,
            sizeof(g_guest_proc_meminfo) - 1U, &g_guest_proc_meminfo_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_UPTIME_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_uptime_file,
            sizeof(g_guest_proc_uptime_file) - 1U, &g_guest_proc_uptime_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_LOADAVG_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_loadavg,
            sizeof(g_guest_proc_loadavg) - 1U, &g_guest_proc_loadavg_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_CPUSTAT_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_cpustat,
            sizeof(g_guest_proc_cpustat) - 1U, &g_guest_proc_cpustat_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_STATUS_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_status,
            sizeof(g_guest_proc_status) - 1U, &g_guest_proc_status_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_MOUNTS_FD) {
        bfree_guest_proc_mounts_ensure();
        return bfree_guest_read_blob(buf, count, g_guest_proc_mounts_buf,
            g_guest_proc_mounts_len, &g_guest_proc_mounts_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_PID2STAT_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_pid2_stat,
            sizeof(g_guest_proc_pid2_stat) - 1U, &g_guest_proc_pid2_stat_off);
    }
    if (fd == (long)BFREE_GUEST_PROC_PID2CMDLINE_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_proc_pid2_cmdline,
            sizeof(g_guest_proc_pid2_cmdline) - 1U, &g_guest_proc_pid2_cmdline_off);
    }
    if (fd == (long)BFREE_GUEST_PASSWD_FD) {
        const char *src = g_guest_etc_passwd;
        size_t total = sizeof(g_guest_etc_passwd) - 1U;
        size_t off = g_guest_etc_passwd_off;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (off >= total) {
            return 0;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > total - off) {
            n = total - off;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = (uint8_t)src[off + i];
        }
        g_guest_etc_passwd_off = off + n;
        return (long)n;
    }
    if (fd == (long)BFREE_GUEST_GROUP_FD) {
        const char *src = g_guest_etc_group;
        size_t total = sizeof(g_guest_etc_group) - 1U;
        size_t off = g_guest_etc_group_off;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (off >= total) {
            return 0;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > total - off) {
            n = total - off;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = (uint8_t)src[off + i];
        }
        g_guest_etc_group_off = off + n;
        return (long)n;
    }
    if (fd == (long)BFREE_GUEST_PROFILE_FD) {
        const char *src = g_guest_etc_profile;
        size_t total = sizeof(g_guest_etc_profile) - 1U;
        size_t off = g_guest_etc_profile_off;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (off >= total) {
            return 0;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > total - off) {
            n = total - off;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = (uint8_t)src[off + i];
        }
        g_guest_etc_profile_off = off + n;
        return (long)n;
    }
    if (fd == (long)BFREE_GUEST_MOTD_FD) {
        const char *src = g_guest_etc_motd;
        size_t total = sizeof(g_guest_etc_motd) - 1U;
        size_t off = g_guest_etc_motd_off;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (off >= total) {
            return 0;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > total - off) {
            n = total - off;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = (uint8_t)src[off + i];
        }
        g_guest_etc_motd_off = off + n;
        return (long)n;
    }
    if (fd == (long)BFREE_GUEST_HOSTS_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_etc_hosts,
            sizeof(g_guest_etc_hosts) - 1U, &g_guest_etc_hosts_off);
    }
    if (fd == (long)BFREE_GUEST_RESOLV_FD) {
        return bfree_guest_read_blob(buf, count, g_guest_etc_resolv,
            sizeof(g_guest_etc_resolv) - 1U, &g_guest_etc_resolv_off);
    }
    if (fd == (long)BFREE_GUEST_BUSYBOX_FD) {
        int rc;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        rc = bfree_initrd_read("busybox.elf", g_guest_busybox_off, (void *)(uintptr_t)buf,
                               (uint64_t)count);
        if (rc < 0) {
            return rc;
        }
        g_guest_busybox_off += (size_t)rc;
        return (long)rc;
    }
    {
        int pslot = bfree_pty_slot_from_fd((int)fd);
        if (pslot >= 0 && g_guest_ptys[pslot].used) {
            bfree_pty_t *py = &g_guest_ptys[pslot];
            unsigned char *src;
            size_t *lenp;
            size_t n, i;
            if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
                return -14;
            }
            if (bfree_pty_is_master((int)fd)) {
                src = py->s2m;
                lenp = &py->s2m_len;
            } else {
                src = py->m2s;
                lenp = &py->m2s_len;
            }
            while (*lenp == 0) {
                int peer = bfree_pty_is_master((int)fd) ? py->slave_open : py->master_open;
                if (!peer) {
                    return 0; /* EOF */
                }
                {
                    int er = bfree_guest_sig_take_eintr();
                    if (er < 0) {
                        return er;
                    }
                }
                __asm__ volatile("sti; hlt" ::: "memory");
            }
            n = (size_t)count;
            if (n > *lenp) {
                n = *lenp;
            }
            for (i = 0; i < n; ++i) {
                ((uint8_t *)(uintptr_t)buf)[i] = src[i];
            }
            if (n < *lenp) {
                for (i = 0; i < *lenp - n; ++i) {
                    src[i] = src[i + n];
                }
            }
            *lenp -= n;
            return (long)n;
        }
    }
    if (bfree_guest_is_pipe_rd(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)fd);

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf) || !ps) {
            return -14;
        }
        /* Resync open counts (live + inactive coop snap) before EOF/block. */
        bfree_guest_pipe_reclaim_dead_slots();
        if (!ps->used) {
            return 0;
        }
        if (ps->len == 0) {
            if (ps->wr_open <= 0) {
                /* Always EOF. Do not "heal" fd0 to the console here: ash's
                 * inproc `echo | cat` closes the write end then reads again,
                 * and healing would make cat swallow the serial keyboard. */
                return 0;
            }
            if (ps->nonblock) {
                return -11; /* EAGAIN */
            }
            while (ps->len == 0 && ps->wr_open > 0) {
                if (g_guest_fork_active && g_coop_side == 1) {
                    return bfree_coop_yield_to_parent();
                }
                if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
                    return bfree_coop_yield_to_child();
                }
                {
                    int er = bfree_guest_sig_take_eintr();
                    if (er < 0) {
                        return er;
                    }
                }
                __asm__ volatile("sti; hlt" ::: "memory");
            }
            if (ps->len == 0) {
                return 0;
            }
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > ps->len) {
            n = ps->len;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = ps->buf[i];
        }
        if (n < ps->len) {
            for (i = 0; i < ps->len - n; ++i) {
                ps->buf[i] = ps->buf[i + n];
            }
        }
        ps->len -= n;
        /* H02: after drain, hand off (AS-copy parent-first only).
         * Skip parent handoff while parent is in waitpid (seq-fork). */
        if (g_guest_fork_active && n > 0 && g_coop_parent_started &&
            bfree_process_child_has_private_as()) {
            if (g_coop_side == 0 && g_coop_child_blocked) {
                return bfree_coop_yield_to_child_done((long)n);
            }
            if (g_coop_side == 1 && !g_coop_parent_in_wait) {
                return bfree_coop_yield_to_parent_done((long)n);
            }
        }
        return (long)n;
    }
    if (bfree_guest_is_eventfd(fd)) {
        int idx = bfree_guest_eventfd_index(fd);
        uint64_t *dst;

        if (buf == 0 || count < (long)sizeof(uint64_t) || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (g_guest_eventfd_val[idx] == 0) {
            /* Counter empty: EAGAIN either way — a blocking wait here would
             * stall the single guest thread with no writer to release it. */
            return -11;
        }
        dst = (uint64_t *)(uintptr_t)buf;
        if (idx >= 0 && g_guest_eventfd_sem[idx]) {
            *dst = 1ULL;
            g_guest_eventfd_val[idx] -= 1ULL;
        } else {
            *dst = g_guest_eventfd_val[idx];
            g_guest_eventfd_val[idx] = 0;
        }
        return (long)sizeof(uint64_t);
    }
    {
        long tr = bfree_linux_read_timerfd(fd, buf, count);
        if (tr != -9) { /* -9 = not a timerfd */
            return tr;
        }
    }
    {
        long sr = bfree_linux_read_signalfd(fd, buf, count);
        if (sr != -9) {
            return sr;
        }
    }
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (vf) {
        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (vf->is_dir) {
            return -21; /* EISDIR */
        }
        /* A5 MAP_SHARED: pull live mmap bytes into vf before serving read(). */
        bfree_guest_shared_mmap_sync_vfile((int)(vf - g_guest_vfiles));
        posp = ofd ? &ofd->pos : &vf->pos;
        if (*posp >= vf->len) {
            return 0;
        }
        dst = (uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > vf->len - *posp) {
            n = vf->len - *posp;
        }
        for (i = 0; i < n; ++i) {
            dst[i] = vf->data[*posp + i];
        }
        *posp += n;
        return (long)n;
    }
    if (orig_fd == 0) {
        /* fd 0 resolved to a stale/garbage target (broken dup chain in the
         * shared fd table); heal back to the console instead of EBADF. */
        g_guest_fd_target[0] = -1;
        return bfree_stdin_read_user(buf, count);
    }
    return -9; /* EBADF */
}

static void bfree_guest_console_write(const uint8_t *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i) {
        char c = (char)src[i];

        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc(c);
    }
}

static long sys_linux_write(long fd, long buf, long count)
{
    const uint8_t *src;
    size_t n;
    size_t i;
    size_t *posp;
    int orig_fd = (int)fd;
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;
    int iidx;

    fd = bfree_guest_fd_resolve((int)fd);
    iidx = bfree_inet_from_fd((int)fd);
    if (iidx >= 0 && g_inet_socks[iidx].tcp_pcb >= 0) {
        long n;
        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        /* tcp_min_send pumps handshake; do not surface EINPROGRESS to BusyBox. */
        n = (long)tcp_min_send(g_inet_socks[iidx].tcp_pcb,
                               (const uint8_t *)(uintptr_t)buf, (size_t)count);
        if (n >= 0) {
            g_inet_socks[iidx].connected = 1;
        }
        return n;
    }
    if (bfree_guest_is_eventfd((int)fd) || bfree_guest_is_eventfd(orig_fd)) {
        int efd = bfree_guest_is_eventfd((int)fd) ? (int)fd : orig_fd;
        int idx = bfree_guest_eventfd_index(efd);
        const uint64_t *src;

        if (buf == 0 || count < (long)sizeof(uint64_t) || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        src = (const uint64_t *)(uintptr_t)buf;
        g_guest_eventfd_val[idx] += *src;
        {
            long sw = bfree_gthr_on_eventfd_write();

            if (sw != 0) {
                return sw;
            }
        }
        return (long)sizeof(uint64_t);
    }
    if (fd == (long)BFREE_GUEST_DEV_NULL_FD) {
        /* /dev/null: discard output (keep behavior permissive for shell redirections). */
        return count;
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);

        if (uidx >= 0 && g_unix_socks[uidx].is_dgram) {
            return bfree_unix_dgram_send(uidx, buf, count, 0, 0);
        }
    }
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (vf) {
        size_t room;

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        if (vf->is_symlink) {
            return -22; /* EINVAL */
        }
        if (vf->is_dir) {
            return -21; /* EISDIR */
        }
        if ((vf->seals & (uint32_t)BFREE_F_SEAL_WRITE) != 0U) {
            return -1; /* EPERM */
        }
        posp = ofd ? &ofd->pos : &vf->pos;
        if (ofd && ((unsigned long)ofd->flags &
                    (unsigned long)BFREE_LINUX_O_APPEND) != 0UL) {
            *posp = vf->len;
        }
        /* POSIX: write at the current offset (pos), extending the file as
         * needed. The previous always-append-at-len behaviour left pos at 0
         * after O_TRUNC and broke any later read of the same fd; more
         * importantly it diverged from what tar/musl expect for sequential
         * archive construction. */
        if (*posp > vf->len) {
            *posp = vf->len;
        }
        if (*posp >= BFREE_GUEST_VFILE_SIZE) {
            return -28; /* ENOSPC */
        }
        src = (const uint8_t *)(uintptr_t)buf;
        room = BFREE_GUEST_VFILE_SIZE - *posp;
        n = (size_t)count;
        if (n > room) {
            n = room;
        }
        if ((vf->seals & (uint32_t)BFREE_F_SEAL_GROW) != 0U) {
            if (*posp >= vf->len) {
                return -1; /* EPERM */
            }
            if (n > vf->len - *posp) {
                n = vf->len - *posp;
            }
        }
        for (i = 0; i < n; ++i) {
            vf->data[*posp + i] = src[i];
        }
        *posp += n;
        if (*posp > vf->len) {
            vf->len = *posp;
        }
        bfree_persist_maybe_flush(vf);
        if (vf->name[0] != '\0') {
            bfree_inotify_notify_vname(vf->name, BFREE_IN_MODIFY);
        }
        return (long)n;
    }
    if (fd == 1 || fd == 2) {
        if (count > 0 && buf != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)buf)) {
            bfree_guest_console_write((const uint8_t *)(uintptr_t)buf, (size_t)count);
            return (long)count;
        }
        return 0;
    }
    {
        int pslot = bfree_pty_slot_from_fd((int)fd);
        if (pslot >= 0 && g_guest_ptys[pslot].used) {
            bfree_pty_t *py = &g_guest_ptys[pslot];
            unsigned char *dst;
            size_t *lenp;
            size_t n, i, room;
            const uint8_t *src;
            if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
                return -14;
            }
            if (bfree_pty_is_master((int)fd)) {
                dst = py->m2s;
                lenp = &py->m2s_len;
            } else {
                dst = py->s2m;
                lenp = &py->s2m_len;
            }
            room = BFREE_PTY_BUF - *lenp;
            if (room == 0) {
                return -11; /* EAGAIN */
            }
            n = (size_t)count;
            if (n > room) {
                n = room;
            }
            src = (const uint8_t *)(uintptr_t)buf;
            for (i = 0; i < n; ++i) {
                dst[*lenp + i] = src[i];
            }
            *lenp += n;
            return (long)n;
        }
    }
    if (bfree_guest_is_pipe_wr(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)fd);

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf) || !ps) {
            return -14;
        }
        /* Resync before EPIPE: parent may have closed an end the child still holds. */
        bfree_guest_pipe_reclaim_dead_slots();
        if (!ps->used) {
            return -32; /* EPIPE */
        }
        if (ps->rd_open <= 0) {
            /* No readers: POSIX EPIPE. Heal shell stdout/stderr to console. */
            if ((orig_fd == 1 || orig_fd == 2) && !g_guest_fork_active) {
                g_guest_fd_target[orig_fd] = -1;
                bfree_guest_console_write((const uint8_t *)(uintptr_t)buf, (size_t)count);
                return (long)count;
            }
            return -32; /* EPIPE */
        }
        src = (const uint8_t *)(uintptr_t)buf;
        n = (size_t)count;
        if (n > BFREE_GUEST_PIPE_BUF_SIZE - ps->len) {
            n = BFREE_GUEST_PIPE_BUF_SIZE - ps->len;
        }
        for (i = 0; i < n; ++i) {
            ps->buf[ps->len + i] = src[i];
        }
        ps->len += n;
        /* H02: coop yield after pipe write — return the byte count on resume.
         * Seq-fork: parent sits in waitpid; yielding with the byte count would
         * falsely complete wait and let ash fork the next stage early. */
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked && n > 0) {
            return bfree_coop_yield_to_child_done((long)n);
        }
        if (g_guest_fork_active && g_coop_side == 1 && n > 0 &&
            g_guest_fork_was_as_copy && g_coop_parent_started &&
            !g_coop_parent_in_wait) {
            return bfree_coop_yield_to_parent_done((long)n);
        }
        return (long)n;
    }
    {
        int iidx2 = bfree_inet_from_fd((int)fd);
        if (iidx2 >= 0 && g_inet_socks[iidx2].shut_wr) {
            bfree_guest_sig_raise(13); /* SIGPIPE */
            return -32; /* EPIPE */
        }
        if (iidx2 >= 0 && g_inet_socks[iidx2].connected &&
            g_inet_socks[iidx2].pipe_magic >= 0) {
            return sys_linux_write(g_inet_socks[iidx2].pipe_magic, buf, count);
        }
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);
        if (uidx >= 0 && g_unix_socks[uidx].shut_wr) {
            bfree_guest_sig_raise(13); /* SIGPIPE */
            return -32; /* EPIPE */
        }
        if (uidx >= 0 && g_unix_socks[uidx].connected &&
            g_unix_socks[uidx].pipe_magic >= 0) {
            return sys_linux_write(g_unix_socks[uidx].pipe_magic, buf, count);
        }
    }
    /* eventfd handled at top of sys_linux_write */
    if (orig_fd == 1 || orig_fd == 2) {
        /* stdout/stderr resolved to a stale/garbage target; heal back to the
         * console instead of failing with EBADF ("cat: write error"). */
        g_guest_fd_target[orig_fd] = -1;
        if (count > 0 && buf != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)buf)) {
            bfree_guest_console_write((const uint8_t *)(uintptr_t)buf, (size_t)count);
        }
        return count > 0 ? count : 0;
    }
    return -9;
}

static int bfree_linux_path_is_dot_or_slash(const char *path)
{
    if (!path) {
        return 0;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return 1;
    }
    if (path[0] == '.' && path[1] == '\0') {
        return 1;
    }
    if (path[0] == '.' && path[1] == '/' && path[2] == '\0') {
        return 1;
    }
    return 0;
}

static unsigned g_guest_tmpfile_seq;

/* O_TMPFILE: unnamed regular file on the /tmp vfile store. The vnode is
 * orphaned immediately, so it disappears once the last fd closes. */
static long bfree_guest_open_tmpfile(long flags)
{
    char vname[48];
    char num[16];
    size_t i = 0;
    size_t n = 0;
    unsigned v;
    int target;
    int pub;
    int accmode = (int)(flags & BFREE_LINUX_O_ACCMODE);
    bfree_guest_vfile_t *vf;

    if (accmode != 1 && accmode != 2) {
        return -22; /* EINVAL: O_TMPFILE needs O_WRONLY or O_RDWR */
    }
    vname[0] = 't';
    vname[1] = 'm';
    vname[2] = 'p';
    vname[3] = 'f';
    vname[4] = 'i';
    vname[5] = 'l';
    vname[6] = 'e';
    vname[7] = '_';
    v = g_guest_tmpfile_seq++;
    if (v == 0U) {
        num[n++] = '0';
    } else {
        while (v > 0U && n < sizeof(num)) {
            num[n++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }
    while (n > 0U && i + 9U < sizeof(vname)) {
        vname[8 + i] = num[--n];
        ++i;
    }
    vname[8 + i] = '\0';
    target = bfree_guest_vfile_alloc_slot(vname, 1);
    if (target < 0) {
        return target;
    }
    pub = bfree_guest_vfile_publish_open(target, (int)flags, 0);
    if (pub < 0) {
        return pub;
    }
    vf = bfree_guest_vfile_from_fd(target);
    if (vf) {
        vf->orphaned = 1;
        vf->nlink = 0;
        vf->name[0] = '\0';
    }
    if (((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL &&
        pub < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_cloexec[pub] = 1;
    }
    return pub;
}

static long sys_linux_openat(long dirfd, long path_ptr, long flags, long mode)
{
    char path[192];
    char vname[64];
    int accmode;
    int want_dir;
    int want_create;
    int want_trunc;
    int want_excl;
    int want_path;
    bfree_guest_vfile_t *vf;
    long path_err;

    (void)mode;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14; /* EFAULT */
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    /* musl shm_open → open("/dev/shm/<name>"); map into /tmp/shm/ vfiles. */
    if (strncmp(path, "/dev/shm/", 9) == 0 && path[9] != '\0') {
        char alt[192];
        size_t i;
        bfree_guest_vfile_t *dir;

        dir = bfree_guest_vfile_find_by_name("shm");
        if (!dir) {
            int dfd = bfree_guest_vfile_alloc_slot("shm", 1);
            if (dfd < 0) {
                return dfd;
            }
            dir = bfree_guest_vfile_from_fd(dfd);
            if (dir) {
                dir->is_dir = 1;
            }
        }
        alt[0] = '/';
        alt[1] = 't';
        alt[2] = 'm';
        alt[3] = 'p';
        alt[4] = '/';
        alt[5] = 's';
        alt[6] = 'h';
        alt[7] = 'm';
        alt[8] = '/';
        for (i = 0; path[9 + i] != '\0' && i + 10U < sizeof(alt); ++i) {
            alt[9 + i] = path[9 + i];
        }
        alt[9 + i] = '\0';
        for (i = 0; alt[i] != '\0' && i + 1U < sizeof(path); ++i) {
            path[i] = alt[i];
        }
        path[i] = '\0';
    }
    if (path[0] == '/' && path[1] == 'r' && path[2] == 'o' && path[3] == 'o' &&
        path[4] == 't' && path[5] == '/') {
        return -2; /* ENOENT — Qt embeds build-machine paths (qtlogging.ini) */
    }
    /* O_TMPFILE implies O_DIRECTORY on Linux; test it before want_dir. */
    if (((unsigned long)flags & (unsigned long)BFREE_LINUX_O_TMPFILE) ==
        (unsigned long)BFREE_LINUX_O_TMPFILE) {
        return bfree_guest_open_tmpfile(flags);
    }
    want_path = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_PATH) != 0UL;
    if (want_path) {
        /* O_PATH: fd refers to the name only — no create/truncate/access mode. */
        flags &= ~(long)(BFREE_LINUX_O_CREAT | BFREE_LINUX_O_TRUNC |
                         BFREE_LINUX_O_APPEND | BFREE_LINUX_O_ACCMODE);
        flags |= (long)BFREE_LINUX_O_PATH;
    }
    accmode = (int)(flags & BFREE_LINUX_O_ACCMODE);
    want_dir = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_DIRECTORY) != 0UL;
    want_create = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CREAT) != 0UL;
    want_trunc = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_TRUNC) != 0UL;
    want_excl = ((unsigned long)flags & 0200UL) != 0UL; /* O_EXCL */

    if (bfree_linux_path_is_dot_or_slash(path) ||
        (want_dir && bfree_linux_path_is_dot_or_slash(path))) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_ROOT_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/") == 0 || (want_dir && strcmp(path, "/") == 0)) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_ROOT_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/tmp") == 0 || (want_dir && bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) && vname[0] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_TMP_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/bin") == 0 || (want_dir && strncmp(path, "/bin/", 5) == 0 && path[5] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_BIN_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/usr") == 0 || (want_dir && strncmp(path, "/usr/", 5) == 0 && path[5] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_USR_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/usr/bin") == 0 || (want_dir && strncmp(path, "/usr/bin/", 9) == 0 && path[9] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_BIN_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/var") == 0 || (want_dir && strncmp(path, "/var/", 5) == 0 && path[5] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_VAR_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/home") == 0 || (want_dir && strncmp(path, "/home/", 6) == 0 && path[6] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_HOME_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/persist") == 0 || (want_dir && strncmp(path, "/persist/", 9) == 0 && path[9] == '\0')) {
        bfree_persist_load_once();
        return bfree_guest_vfile_publish_open(BFREE_GUEST_PERSIST_DIR_FD, (int)flags, 0);
    }
    if (strncmp(path, "/bin/", 5) == 0 && path[5] != '\0') {
        g_guest_busybox_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_BUSYBOX_FD);
    }
    if (strncmp(path, "/usr/bin/", 9) == 0 && path[9] != '\0') {
        g_guest_busybox_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_BUSYBOX_FD);
    }
    if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) && vname[0] != '\0') {
        vf = bfree_guest_vfile_find_by_name(vname);
        if (vf && want_create && want_excl) {
            return -17; /* EEXIST */
        }
        if (vf && vf->is_symlink) {
            /* Follow one level: only /tmp targets can exist here anyway. */
            char lname[64];

            if (bfree_guest_path_is_under_tmp((const char *)vf->data, lname,
                    sizeof(lname)) && lname[0] != '\0') {
                memcpy(vname, lname, sizeof(vname) < sizeof(lname) ?
                    sizeof(vname) : sizeof(lname));
                vf = bfree_guest_vfile_find_by_name(vname);
            } else {
                return -2;
            }
        }
        if (vf && vf->is_dir) {
            int target;

            if (want_create && (accmode == 1 || accmode == 2) && !want_dir) {
                return -21; /* EISDIR */
            }
            if (accmode == 1 || accmode == 2) {
                return -21; /* EISDIR */
            }
            target = (int)BFREE_GUEST_VFILE_FD_BASE +
                (int)(vf - g_guest_vfiles);
            return bfree_guest_vfile_publish_open(target, (int)flags, 0);
        }
        if (want_dir) {
            return vf ? -20 : -2; /* ENOTDIR / ENOENT */
        }
        if (accmode == 1 || accmode == 2) { /* write or read-write */
            int truncate = want_trunc || (want_create && vf == 0);
            int target;
            size_t initial_pos = 0;
            int was_new = (vf == 0);

            if (!vf && !want_create) {
                return -2;
            }
            if (!vf && !bfree_guest_tmp_parents_exist(vname)) {
                return -2; /* ENOENT: missing parent directory */
            }
            target = bfree_guest_vfile_alloc_slot(vname, truncate);
            if (target < 0) {
                return target;
            }
            vf = bfree_guest_vfile_from_fd(target);
            if (vf && vf->is_dir) {
                return -21;
            }
            if (vf && ((unsigned long)flags &
                       (unsigned long)BFREE_LINUX_O_APPEND) != 0UL) {
                initial_pos = vf->len;
            }
            {
                long pub = bfree_guest_vfile_publish_open(target, (int)flags,
                                                          initial_pos);

                if (pub >= 0 && was_new) {
                    bfree_inotify_notify_vname(vname, BFREE_IN_CREATE);
                }
                return pub;
            }
        }
        if (vf) {
            int target = (int)BFREE_GUEST_VFILE_FD_BASE +
                (int)(vf - g_guest_vfiles);

            return bfree_guest_vfile_publish_open(target, (int)flags, 0);
        }
        return -2;
    }
    if (strcmp(path, "/dev/fb0") == 0 || strcmp(path, "/dev/fb") == 0) {
        return BFREE_FB0_FD;
    }
    if (strcmp(path, "/dev/input0") == 0 || strcmp(path, "/dev/input/event0") == 0) {
        return BFREE_INPUT_EVENT_FD;
    }
    if (strcmp(path, "/dev/null") == 0) {
        /* Publish as small guest fd so close/dup2 behave normally. */
        return bfree_guest_fd_publish(BFREE_GUEST_DEV_NULL_FD);
    }
    if (strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/random") == 0) {
        return bfree_guest_fd_publish(BFREE_GUEST_DEV_URANDOM_FD);
    }
    if (strcmp(path, "/dev/tty") == 0) {
        return bfree_guest_fd_publish(BFREE_GUEST_DEV_TTY_FD);
    }
    if (strcmp(path, "/dev/pts") == 0) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_PTS_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/dev/ptmx") == 0) {
        int s;
        for (s = 0; s < BFREE_PTY_SLOTS; ++s) {
            if (!g_guest_ptys[s].used) {
                g_guest_ptys[s].used = 1;
                g_guest_ptys[s].master_open = 1;
                g_guest_ptys[s].slave_open = 0;
                g_guest_ptys[s].m2s_len = 0;
                g_guest_ptys[s].s2m_len = 0;
                return bfree_guest_fd_publish((int)BFREE_PTY_MASTER_BASE + s);
            }
        }
        return -24; /* EMFILE */
    }
    if (strncmp(path, "/dev/pts/", 9) == 0 && path[9] >= '0' && path[9] <= '9' && path[10] == '\0') {
        int s = path[9] - '0';
        if (s < 0 || s >= BFREE_PTY_SLOTS || !g_guest_ptys[s].used) {
            return -2;
        }
        g_guest_ptys[s].slave_open = 1;
        return bfree_guest_fd_publish((int)BFREE_PTY_SLAVE_BASE + s);
    }
    if (strcmp(path, "/proc") == 0 || (want_dir && strncmp(path, "/proc/", 6) == 0 && path[6] == '\0')) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_PROC_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/proc/self") == 0 || strcmp(path, "/proc/1") == 0 ||
        strcmp(path, "/proc/2") == 0 ||
        strcmp(path, "/proc/self/") == 0 || strcmp(path, "/proc/1/") == 0 ||
        strcmp(path, "/proc/2/") == 0) {
        return bfree_guest_vfile_publish_open(BFREE_GUEST_PROC_PID_DIR_FD, (int)flags, 0);
    }
    if (strcmp(path, "/proc/self/maps") == 0 || strcmp(path, "/proc/1/maps") == 0) {
        if (want_dir) {
            return -20; /* ENOTDIR */
        }
        g_guest_proc_maps_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_MAPS_FD);
    }
    if (strcmp(path, "/proc/self/stat") == 0 || strcmp(path, "/proc/1/stat") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_pid_stat_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_PIDSTAT_FD);
    }
    if (strcmp(path, "/proc/2/stat") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_pid2_stat_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_PID2STAT_FD);
    }
    if (strcmp(path, "/proc/self/cmdline") == 0 || strcmp(path, "/proc/1/cmdline") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_cmdline_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_CMDLINE_FD);
    }
    if (strcmp(path, "/proc/2/cmdline") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_pid2_cmdline_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_PID2CMDLINE_FD);
    }
    if (strcmp(path, "/proc/self/status") == 0 || strcmp(path, "/proc/1/status") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_status_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_STATUS_FD);
    }
    if (strcmp(path, "/proc/meminfo") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_meminfo_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_MEMINFO_FD);
    }
    if (strcmp(path, "/proc/uptime") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_uptime_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_UPTIME_FD);
    }
    if (strcmp(path, "/proc/loadavg") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_loadavg_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_LOADAVG_FD);
    }
    if (strcmp(path, "/proc/stat") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_cpustat_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_CPUSTAT_FD);
    }
    if (strcmp(path, "/proc/mounts") == 0 || strcmp(path, "/proc/self/mounts") == 0) {
        if (want_dir) {
            return -20;
        }
        g_guest_proc_mounts_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROC_MOUNTS_FD);
    }
    if (strcmp(path, "/etc/passwd") == 0) {
        g_guest_etc_passwd_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PASSWD_FD);
    }
    if (strcmp(path, "/etc/group") == 0) {
        g_guest_etc_group_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_GROUP_FD);
    }
    if (strcmp(path, "/etc/profile") == 0) {
        g_guest_etc_profile_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_PROFILE_FD);
    }
    if (strcmp(path, "/etc/motd") == 0) {
        g_guest_etc_motd_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_MOTD_FD);
    }
    if (strcmp(path, "/etc/hosts") == 0) {
        g_guest_etc_hosts_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_HOSTS_FD);
    }
    if (strcmp(path, "/etc/resolv.conf") == 0) {
        g_guest_etc_resolv_off = 0;
        return bfree_guest_fd_publish(BFREE_GUEST_RESOLV_FD);
    }
    if (strcmp(path, g_guest_busybox_exe_path) == 0) {
        g_guest_busybox_off = 0;
        return BFREE_GUEST_BUSYBOX_FD;
    }
    return -2; /* ENOENT */
}

static long sys_linux_chdir(long path_ptr)
{
    char path[256];
    size_t len;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    bfree_guest_path_absolutize(path, sizeof(path));
    /* Validate path exists and is a directory */
    if (bfree_linux_path_is_dot_or_slash(path)) {
        g_guest_cwd[0] = '/';
        g_guest_cwd[1] = '\0';
        return 0;
    }
    if (strcmp(path, "/") == 0) {
        g_guest_cwd[0] = '/';
        g_guest_cwd[1] = '\0';
        return 0;
    }
    if (strcmp(path, "/etc") == 0 ||
        strcmp(path, "/bin") == 0 ||
        strcmp(path, "/usr") == 0 ||
        strcmp(path, "/usr/bin") == 0 ||
        strcmp(path, "/var") == 0 ||
        strcmp(path, "/root") == 0 ||
        strcmp(path, "/tmp") == 0 ||
        strcmp(path, "/dev") == 0 ||
        strcmp(path, "/proc") == 0) {
        len = 0;
        while (path[len] != '\0' && len < sizeof(g_guest_cwd) - 1) {
            g_guest_cwd[len] = path[len];
            ++len;
        }
        g_guest_cwd[len] = '\0';
        return 0;
    }
    {
        char vname[64];
        bfree_guest_vfile_t *vf;

        if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) &&
            vname[0] != '\0') {
            vf = bfree_guest_vfile_find_by_name(vname);
            if (!vf) {
                return -2;
            }
            if (!vf->is_dir) {
                return -20; /* ENOTDIR */
            }
            len = 0;
            while (path[len] != '\0' && len < sizeof(g_guest_cwd) - 1) {
                g_guest_cwd[len] = path[len];
                ++len;
            }
            g_guest_cwd[len] = '\0';
            return 0;
        }
    }
    return -2; /* ENOENT */
}

static long sys_linux_utimensat(long dirfd, long pathname_ptr, long times_ptr, long flags)
{
    /* Linux x86_64 timespec is {int64_t tv_sec; int64_t tv_nsec;}. */
    typedef struct {
        int64_t tv_sec;
        int64_t tv_nsec;
    } bfree_timespec64_t;
    enum { BFREE_UTIME_NOW = ((1L << 30) - 1), BFREE_UTIME_OMIT = ((1L << 30) - 2) };
    bfree_timespec64_t times[2];
    bfree_guest_vfile_t *vf = 0;
    char path[256];
    char vname[64];
    uint64_t us;
    int64_t now_sec;
    int64_t now_nsec;
    int i;

    (void)flags;
    us = knl_get_current_time();
    now_sec = (int64_t)(us / 1000000ULL);
    now_nsec = (int64_t)((us % 1000000ULL) * 1000ULL);

    if (pathname_ptr == 0) {
        /* futimens(fd): dirfd is the open file descriptor. */
        int resolved;
        if (dirfd < 0) {
            return -9; /* EBADF */
        }
        resolved = bfree_guest_fd_resolve((int)dirfd);
        if (resolved < 0) {
            return -9;
        }
        vf = bfree_guest_vfile_from_open_fd(resolved, 0);
        if (!vf) {
            /* Non-vfile fds: accept and ignore (clocks not stored). */
            return 0;
        }
    } else {
        long path_err;
        if (copy_user_cstr(pathname_ptr, path, sizeof(path)) != 0) {
            return -14;
        }
        path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
        if (path_err != 0) {
            return path_err;
        }
        /* /dev/null/invalid → ENOTDIR (null is not a directory). */
        if (strncmp(path, "/dev/null/", 10) == 0) {
            return -20; /* ENOTDIR */
        }
        if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) &&
            vname[0] != '\0') {
            vf = bfree_guest_vfile_find_by_name(vname);
        }
        if (!vf) {
            /* Paths we don't track: succeed without storing (POSIX soft). */
            return 0;
        }
    }

    if (times_ptr == 0) {
        times[0].tv_sec = now_sec;
        times[0].tv_nsec = now_nsec;
        times[1].tv_sec = now_sec;
        times[1].tv_nsec = now_nsec;
    } else {
        if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)times_ptr, sizeof(times))) {
            return -14;
        }
        for (i = 0; i < 2; ++i) {
            times[i] = ((bfree_timespec64_t *)(uintptr_t)times_ptr)[i];
        }
    }

    for (i = 0; i < 2; ++i) {
        int64_t sec = times[i].tv_sec;
        int64_t nsec = times[i].tv_nsec;
        if (nsec == BFREE_UTIME_OMIT) {
            continue;
        }
        if (nsec == BFREE_UTIME_NOW) {
            sec = now_sec;
            nsec = now_nsec;
        } else if (nsec < 0 || nsec >= 1000000000LL) {
            return -22; /* EINVAL */
        }
        if (i == 0) {
            vf->atime_sec = sec;
            vf->atime_nsec = nsec;
        } else {
            vf->mtime_sec = sec;
            vf->mtime_nsec = nsec;
        }
    }
    return 0;
}

/* Linux 235: utimes(path, timeval[2]) — convert µs → nsec via utimensat path. */
static long sys_linux_utimes(long path_ptr, long times_ptr)
{
    typedef struct {
        int64_t tv_sec;
        int64_t tv_usec;
    } bfree_timeval64_t;
    typedef struct {
        int64_t tv_sec;
        int64_t tv_nsec;
    } bfree_timespec64_t;
    bfree_timeval64_t tv[2];
    bfree_timespec64_t ts[2];
    bfree_guest_vfile_t *vf = 0;
    char path[256];
    char vname[64];
    long path_err;
    int i;

    if (path_ptr == 0) {
        return -14;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(BFREE_LINUX_AT_FDCWD, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) &&
        vname[0] != '\0') {
        vf = bfree_guest_vfile_find_by_name(vname);
    }
    if (!vf) {
        return 0; /* untracked path: soft success */
    }
    if (times_ptr == 0) {
        uint64_t us = knl_get_current_time();
        int64_t sec = (int64_t)(us / 1000000ULL);
        int64_t nsec = (int64_t)((us % 1000000ULL) * 1000ULL);
        vf->atime_sec = sec;
        vf->atime_nsec = nsec;
        vf->mtime_sec = sec;
        vf->mtime_nsec = nsec;
        return 0;
    }
    if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)times_ptr, sizeof(tv))) {
        return -14;
    }
    for (i = 0; i < 2; ++i) {
        tv[i] = ((bfree_timeval64_t *)(uintptr_t)times_ptr)[i];
        if (tv[i].tv_usec < 0 || tv[i].tv_usec >= 1000000) {
            return -22;
        }
        ts[i].tv_sec = tv[i].tv_sec;
        ts[i].tv_nsec = tv[i].tv_usec * 1000;
    }
    vf->atime_sec = ts[0].tv_sec;
    vf->atime_nsec = ts[0].tv_nsec;
    vf->mtime_sec = ts[1].tv_sec;
    vf->mtime_nsec = ts[1].tv_nsec;
    return 0;
}

static long sys_linux_access(long path_ptr, long mode)
{
    char path[256];
    char vname[64];

    (void)mode;
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    bfree_guest_path_absolutize(path, sizeof(path));
    if (bfree_linux_path_is_dot_or_slash(path)) {
        return 0;
    }
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' &&
        (path[4] == '/' || path[4] == '\0')) {
        return 0;
    }
    if (path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' &&
        path[4] == 'c' && (path[5] == '/' || path[5] == '\0')) {
        return 0;
    }
    if (strcmp(path, g_guest_busybox_exe_path) == 0) {
        return 0;
    }
    if (strcmp(path, "/etc") == 0 ||
        strcmp(path, "/etc/passwd") == 0 || strcmp(path, "/etc/group") == 0 ||
        strcmp(path, "/etc/profile") == 0 || strcmp(path, "/etc/motd") == 0 ||
        strcmp(path, "/etc/hosts") == 0 || strcmp(path, "/etc/resolv.conf") == 0) {
        return 0;
    }
    if (strcmp(path, "/bin") == 0 || strncmp(path, "/bin/", 5) == 0) {
        return 0;
    }
    if (strcmp(path, "/usr") == 0 || strncmp(path, "/usr/", 4) == 0) {
        return 0;
    }
    if (strcmp(path, "/var") == 0 || strncmp(path, "/var/", 4) == 0) {
        return 0;
    }
    if (strcmp(path, "/root") == 0) {
        return 0;
    }
    if (strcmp(path, "/tmp") == 0) {
        return 0;
    }
    if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) && vname[0] != '\0') {
        if (bfree_guest_vfile_find_by_name(vname)) {
            return 0;
        }
    }
    return -2;
}

/* Path-based access after dirfd resolution (Linux faccessat). */
static long sys_linux_faccessat(long dirfd, long path_ptr, long mode, long flags)
{
    char path[256];
    long path_err;

    (void)flags;
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    /* Reuse access checks via a temporary absolute path in g_guest_cwd style:
     * sys_linux_access reads from user memory, so inline the same checks. */
    (void)mode;
    {
        char vname[64];

        if (bfree_linux_path_is_dot_or_slash(path) || strcmp(path, "/") == 0) {
            return 0;
        }
        if (strcmp(path, "/tmp") == 0 || strcmp(path, "/bin") == 0 ||
            strcmp(path, "/usr") == 0 || strcmp(path, "/var") == 0 ||
            strcmp(path, "/home") == 0 || strcmp(path, "/persist") == 0 ||
            strcmp(path, "/dev") == 0 || strcmp(path, "/proc") == 0) {
            return 0;
        }
        if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) &&
            vname[0] != '\0') {
            return bfree_guest_vfile_find_by_name(vname) ? 0 : -2;
        }
        return -2;
    }
}

static long sys_linux_fchdir(long fd)
{
    char path[256];
    size_t len;
    long path_err;

    path[0] = '.';
    path[1] = '\0';
    path_err = bfree_guest_path_at(fd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (bfree_linux_path_is_dot_or_slash(path) || strcmp(path, "/") == 0) {
        g_guest_cwd[0] = '/';
        g_guest_cwd[1] = '\0';
        return 0;
    }
    if (strcmp(path, "/tmp") != 0 && strcmp(path, "/bin") != 0 &&
        strcmp(path, "/usr") != 0 && strcmp(path, "/usr/bin") != 0 &&
        strcmp(path, "/var") != 0 && strcmp(path, "/home") != 0 &&
        strcmp(path, "/persist") != 0 && strcmp(path, "/proc") != 0 &&
        strcmp(path, "/dev") != 0 && strcmp(path, "/root") != 0 &&
        strcmp(path, "/etc") != 0) {
        char vname[64];
        bfree_guest_vfile_t *vf;

        if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
            vname[0] == '\0') {
            return -20; /* ENOTDIR */
        }
        vf = bfree_guest_vfile_find_by_name(vname);
        if (!vf || !vf->is_dir) {
            return -20;
        }
    }
    len = 0;
    while (path[len] != '\0' && len + 1U < sizeof(g_guest_cwd)) {
        g_guest_cwd[len] = path[len];
        ++len;
    }
    g_guest_cwd[len] = '\0';
    return 0;
}

static long sys_linux_close(long fd)
{
    int orig = (int)fd;
    int resolved;
    int refs = 0;
    int i;

    resolved = bfree_guest_fd_resolve(orig);
    if (orig >= 0 && orig < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_target[orig] = -1;
        g_guest_fd_dup_save[orig] = -1;
        g_guest_fd_cloexec[orig] = 0;
    }
    bfree_pidfd_release(resolved);
    bfree_inotify_release(resolved);
    bfree_fanotify_release(resolved);
    bfree_perf_release(resolved);
    bfree_fsctx_release(resolved);
    bfree_iouring_release(resolved);
    bfree_uffd_release(resolved);
    bfree_landlock_release(resolved);
    {
        int sfi = resolved - (int)BFREE_SIGNALFD_FD_BASE;
        if (sfi >= 0 && sfi < BFREE_MAX_SIGNALFD && g_guest_signalfds[sfi].used) {
            g_guest_signalfds[sfi].used = 0;
            g_guest_signalfds[sfi].mask = 0;
            g_guest_signalfds[sfi].nonblock = 0;
        }
    }
    /* BusyBox ping xmove_fd(sock,0): dup2 then close(old). Keep inet sock
     * alive while another published fd still resolves to it. */
    if (resolved >= (int)BFREE_INET_FD_BASE &&
        resolved < (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            if (g_guest_fd_target[i] == resolved) {
                refs++;
            }
        }
        if (refs == 0) {
            bfree_inet_sock_release(resolved);
        }
    } else {
        bfree_inet_sock_release(resolved);
    }
    /* Free AF_UNIX socketpair/connect slots when last published fd closes. */
    {
        int uidx = bfree_unix_from_fd(resolved);
        if (uidx >= 0) {
            refs = 0;
            for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
                if (g_guest_fd_target[i] == resolved) {
                    refs++;
                }
            }
            if (refs == 0) {
                g_unix_socks[uidx].used = 0;
                g_unix_socks[uidx].listening = 0;
                g_unix_socks[uidx].connected = 0;
                g_unix_socks[uidx].is_dgram = 0;
                g_unix_socks[uidx].accept_rd = -1;
                g_unix_socks[uidx].accept_wr = -1;
                g_unix_socks[uidx].pipe_magic = -1;
                g_unix_socks[uidx].path[0] = '\0';
                g_unix_socks[uidx].peer_path[0] = '\0';
                g_unix_socks[uidx].dg_pending = 0;
                g_unix_socks[uidx].dg_len = 0;
                bfree_sock_accept_q_reset(g_unix_socks[uidx].q_rd,
                                          g_unix_socks[uidx].q_wr,
                                          &g_unix_socks[uidx].q_len,
                                          &g_unix_socks[uidx].listen_backlog);
                bfree_guest_pipe_reclaim_dead_slots();
            }
        }
    }
    /* Absolute recount (live + inactive coop snap) — do not pipe_ref±1 here;
     * a parent close must not drop the child's still-parked endpoint to 0. */
    if (bfree_guest_is_pipe_wr(resolved) || bfree_guest_is_pipe_rd(resolved)) {
        bfree_guest_pipe_reclaim_dead_slots();
    }
    bfree_guest_ofd_maybe_release(resolved);
    return 0;
}

/* Resolve a published fd to its backing magic/vfile target (OFD or raw). */
static int bfree_guest_open_target(int fd, bfree_guest_ofd_t **ofd_out)
{
    bfree_guest_ofd_t *ofd = bfree_guest_ofd_from_fd(fd);

    if (ofd_out) {
        *ofd_out = ofd;
    }
    if (ofd) {
        return ofd->target;
    }
    return fd;
}

static long sys_linux_fcntl(long fd, long cmd, long arg)
{
    int resolved;
    int minfd;
    int i;

    if (fd < 0) {
        return -9;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved < 0) {
        return -9;
    }

    switch (cmd) {
    case 0: /* F_DUPFD */
    case 1030: /* F_DUPFD_CLOEXEC */
        minfd = (int)arg;
        if (minfd < 3 || minfd >= BFREE_GUEST_FD_TABLE_SIZE) {
            minfd = 3;
        }
        for (i = minfd; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            if ((uint64_t)i >= g_rlim_nofile_cur) {
                return -24; /* EMFILE: RLIMIT_NOFILE */
            }
            if (g_guest_fd_target[i] < 0 && g_guest_fd_dup_save[i] < 0) {
                g_guest_fd_target[i] = resolved;
                g_guest_fd_dup_save[i] = resolved;
                g_guest_fd_cloexec[i] = (cmd == 1030) ? 1 : 0;
                return i;
            }
        }
        return -24;
    case 1: /* F_GETFD */
        /* Published slots with no target are closed (resolve alone is not enough). */
        if (fd > 2 && fd < BFREE_GUEST_FD_TABLE_SIZE &&
            g_guest_fd_target[fd] < 0 && g_guest_fd_dup_save[fd] < 0) {
            return -9; /* EBADF */
        }
        if (fd >= 0 && fd < BFREE_GUEST_FD_TABLE_SIZE && g_guest_fd_cloexec[fd]) {
            return 1; /* FD_CLOEXEC */
        }
        return 0;
    case 2: /* F_SETFD */
        if (fd > 2 && fd < BFREE_GUEST_FD_TABLE_SIZE &&
            g_guest_fd_target[fd] < 0 && g_guest_fd_dup_save[fd] < 0) {
            return -9;
        }
        if (fd >= 0 && fd < BFREE_GUEST_FD_TABLE_SIZE) {
            g_guest_fd_cloexec[fd] = ((arg & 1) != 0) ? 1 : 0;
        }
        return 0;
    case 3: /* F_GETFL */
        if (resolved == 1 || resolved == 2) {
            return 1;
        }
        if (bfree_guest_ofd_from_fd(resolved)) {
            return bfree_guest_ofd_from_fd(resolved)->flags;
        }
        if (bfree_guest_is_vfile_fd(resolved)) {
            return 1;
        }
        {
            int iidx = bfree_inet_from_fd(resolved);
            int uidx;
            int pslot = bfree_guest_pipe_slot_from_magic(resolved);
            long fl = 0;

            if (bfree_guest_is_eventfd(resolved) &&
                g_guest_eventfd_nonblock[bfree_guest_eventfd_index(resolved)]) {
                return (long)BFREE_LINUX_O_NONBLOCK;
            }
            {
                int sfi = resolved - (int)BFREE_SIGNALFD_FD_BASE;

                if (sfi >= 0 && sfi < BFREE_MAX_SIGNALFD &&
                    g_guest_signalfds[sfi].used) {
                    return g_guest_signalfds[sfi].nonblock
                               ? (long)BFREE_LINUX_O_NONBLOCK
                               : 0;
                }
            }
            if (iidx >= 0 && g_inet_socks[iidx].nonblock) {
                fl |= (long)BFREE_LINUX_O_NONBLOCK;
            }
            uidx = bfree_unix_from_fd(resolved);
            if (uidx >= 0 && g_unix_socks[uidx].nonblock) {
                fl |= (long)BFREE_LINUX_O_NONBLOCK;
            }
            if (pslot >= 0 && g_guest_pipes[pslot].used &&
                g_guest_pipes[pslot].nonblock) {
                fl |= (long)BFREE_LINUX_O_NONBLOCK;
            }
            return fl;
        }
    case 4: /* F_SETFL */
        {
            int pslot = bfree_guest_pipe_slot_from_magic(resolved);
            int nb = ((unsigned long)arg & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;
            int iidx = bfree_inet_from_fd(resolved);
            int uidx = bfree_unix_from_fd(resolved);

            if (bfree_guest_is_eventfd(resolved)) {
                g_guest_eventfd_nonblock[bfree_guest_eventfd_index(resolved)] = nb;
            }
            {
                int sfi = resolved - (int)BFREE_SIGNALFD_FD_BASE;

                if (sfi >= 0 && sfi < BFREE_MAX_SIGNALFD &&
                    g_guest_signalfds[sfi].used) {
                    g_guest_signalfds[sfi].nonblock = nb;
                }
            }
            if (pslot >= 0 && g_guest_pipes[pslot].used) {
                g_guest_pipes[pslot].nonblock = nb;
            }
            if (iidx >= 0) {
                g_inet_socks[iidx].nonblock = nb;
                if (g_inet_socks[iidx].accept_rd >= 0) {
                    int ps = bfree_guest_pipe_slot_from_magic(g_inet_socks[iidx].accept_rd);
                    if (ps >= 0) {
                        g_guest_pipes[ps].nonblock = nb;
                    }
                }
                if (g_inet_socks[iidx].pipe_magic >= 0) {
                    int ps = bfree_guest_pipe_slot_from_magic(g_inet_socks[iidx].pipe_magic);
                    if (ps >= 0) {
                        g_guest_pipes[ps].nonblock = nb;
                    }
                }
            }
            if (uidx >= 0) {
                g_unix_socks[uidx].nonblock = nb;
                if (g_unix_socks[uidx].accept_rd >= 0) {
                    int ps = bfree_guest_pipe_slot_from_magic(g_unix_socks[uidx].accept_rd);
                    if (ps >= 0) {
                        g_guest_pipes[ps].nonblock = nb;
                    }
                }
                if (g_unix_socks[uidx].pipe_magic >= 0) {
                    int ps = bfree_guest_pipe_slot_from_magic(g_unix_socks[uidx].pipe_magic);
                    if (ps >= 0) {
                        g_guest_pipes[ps].nonblock = nb;
                    }
                }
            }
        }
        return 0;
    case BFREE_LINUX_F_GETLK:
    case BFREE_LINUX_F_SETLK:
    case BFREE_LINUX_F_SETLKW:
    case 12: /* F_GETLK64 */
    case 13: /* F_SETLK64 */
    case 14: /* F_SETLKW64 */
    case 36: /* F_OFD_GETLK */
    case 37: /* F_OFD_SETLK */
    case 38: /* F_OFD_SETLKW */
        {
            bfree_linux_flock_t fl_user;
            bfree_linux_flock_t *flp;
            bfree_guest_vfile_t *vf;
            bfree_guest_ofd_t *ofd_lk;
            int vnode;
            int self_pid;
            int i;
            int conflict = -1;
            int mine = -1;
            int16_t l_type;
            int is_get;

            if (arg == 0 ||
                !bfree_user_buf_mapped((uint64_t)(uintptr_t)arg, sizeof(fl_user))) {
                return -14;
            }
            /* AS-copy child must poke the child's PT, not a stale parent CR3. */
            if (g_guest_fork_active && g_guest_fork_was_as_copy && g_coop_side == 1) {
                page_table_t *cpt = bfree_process_child_pt();
                if (cpt && knl_current_task) {
                    knl_current_task->page_table_base = cpt;
                    __asm__ volatile("mov %0, %%cr3" :: "r"(cpt) : "memory");
                    g_bfree_sysret_exec_cr3 = 0;
                }
            }
            flp = (bfree_linux_flock_t *)(uintptr_t)arg;
            /* Prefer phys peek: AS-copy child stack may not match identity *flp. */
            if (bfree_user_stack_peek_bytes((uint64_t)(uintptr_t)arg,
                                            (char *)&fl_user, sizeof(fl_user)) != 0) {
                fl_user = *flp;
            }
            l_type = fl_user.l_type;
            vf = bfree_guest_vfile_from_open_fd(resolved, &ofd_lk);
            is_get = (cmd == BFREE_LINUX_F_GETLK || cmd == 12 || cmd == 36);
            if (!vf) {
                /* Fall back: advisory lock keyed by OFD slot (negative vnode). */
                if (ofd_lk) {
                    vnode = -1 - (int)(ofd_lk - g_guest_ofds);
                } else {
                    if (is_get) {
                        fl_user.l_type = (int16_t)BFREE_LINUX_F_UNLCK;
                        fl_user.l_pid = 0;
                        (void)bfree_user_stack_poke_bytes((uint64_t)(uintptr_t)arg,
                                                          (const char *)&fl_user,
                                                          sizeof(fl_user));
                        *flp = fl_user;
                    }
                    return 0;
                }
            } else {
                vnode = (int)(vf - g_guest_vfiles);
            }
            /* Lock owner must match getpid() of the locker. Parent side is
             * always pid 1; never use a stale g_guest_fork_pid on side 0. */
            self_pid = (g_coop_side == 1 && g_guest_fork_active &&
                        g_guest_fork_pid > 0)
                           ? g_guest_fork_pid
                           : 1;

            for (i = 0; i < BFREE_GUEST_FLOCK_SLOTS; ++i) {
                if (!g_guest_flocks[i].used || g_guest_flocks[i].vnode != vnode) {
                    continue;
                }
                if (g_guest_flocks[i].owner_pid == self_pid) {
                    mine = i;
                    continue;
                }
                if (is_get ||
                    g_guest_flocks[i].type == BFREE_LINUX_F_WRLCK ||
                    l_type == BFREE_LINUX_F_WRLCK ||
                    !(l_type == BFREE_LINUX_F_RDLCK &&
                      g_guest_flocks[i].type == BFREE_LINUX_F_RDLCK)) {
                    conflict = i;
                }
            }

            if (is_get) {
                if (conflict >= 0) {
                    fl_user.l_type = (int16_t)g_guest_flocks[conflict].type;
                    fl_user.l_whence = 0;
                    fl_user.l_start = 0;
                    fl_user.l_len = 0;
                    /* sys_linux_getppid() always returns 1 for coop children. */
                    fl_user.l_pid = (g_coop_side == 1) ? 1
                        : g_guest_flocks[conflict].owner_pid;
                } else {
                    fl_user.l_type = (int16_t)BFREE_LINUX_F_UNLCK;
                    fl_user.l_pid = 0;
                }
                /* Always poke phys AND store via VA — COW child must see l_pid. */
                (void)bfree_user_stack_poke_bytes((uint64_t)(uintptr_t)arg,
                                                  (const char *)&fl_user,
                                                  sizeof(fl_user));
                *flp = fl_user;
                return 0;
            }

            /* F_SETLK / F_SETLKW */
            if (l_type == BFREE_LINUX_F_UNLCK) {
                if (mine >= 0) {
                    g_guest_flocks[mine].used = 0;
                }
                return 0;
            }
            if (conflict >= 0) {
                int blocking = (cmd == BFREE_LINUX_F_SETLKW || cmd == 14 ||
                                cmd == 38);
                if (!blocking) {
                    return -11; /* EAGAIN: F_SETLK / OFD_SETLK */
                }
                /* F_SETLKW: park like blocking pipe read (coop/gthr/EINTR). */
                while (conflict >= 0) {
                    if (g_guest_fork_active && g_coop_side == 1) {
                        return bfree_coop_yield_to_parent();
                    }
                    if (g_guest_fork_active && g_coop_side == 0 &&
                        g_coop_child_blocked) {
                        return bfree_coop_yield_to_child();
                    }
                    {
                        long sw = bfree_gthr_yield();
                        if (sw != 0) {
                            return sw;
                        }
                    }
                    {
                        int er = bfree_guest_sig_take_eintr();
                        if (er < 0) {
                            return er;
                        }
                    }
                    __asm__ volatile("sti; pause; cli" ::: "memory");
                    conflict = -1;
                    mine = -1;
                    for (i = 0; i < BFREE_GUEST_FLOCK_SLOTS; ++i) {
                        if (!g_guest_flocks[i].used ||
                            g_guest_flocks[i].vnode != vnode) {
                            continue;
                        }
                        if (g_guest_flocks[i].owner_pid == self_pid) {
                            mine = i;
                            continue;
                        }
                        if (g_guest_flocks[i].type == BFREE_LINUX_F_WRLCK ||
                            l_type == BFREE_LINUX_F_WRLCK ||
                            !(l_type == BFREE_LINUX_F_RDLCK &&
                              g_guest_flocks[i].type == BFREE_LINUX_F_RDLCK)) {
                            conflict = i;
                        }
                    }
                }
            }
            if (mine < 0) {
                for (i = 0; i < BFREE_GUEST_FLOCK_SLOTS; ++i) {
                    if (!g_guest_flocks[i].used) {
                        mine = i;
                        break;
                    }
                }
                if (mine < 0) {
                    return -11;
                }
            }
            g_guest_flocks[mine].used = 1;
            g_guest_flocks[mine].vnode = vnode;
            g_guest_flocks[mine].type = (l_type == BFREE_LINUX_F_RDLCK)
                                            ? BFREE_LINUX_F_RDLCK
                                            : BFREE_LINUX_F_WRLCK;
            g_guest_flocks[mine].owner_pid = self_pid;
            return 0;
        }
    case 1033: /* F_ADD_SEALS */
        {
            bfree_guest_vfile_t *vf = bfree_guest_vfile_from_open_fd(resolved, 0);
            uint32_t add = (uint32_t)arg;

            if (!vf || !bfree_guest_vfile_is_memfd(vf)) {
                return -22; /* EINVAL: sealing needs a memfd */
            }
            if ((add & ~(uint32_t)(BFREE_F_SEAL_SEAL | BFREE_F_SEAL_SHRINK |
                                   BFREE_F_SEAL_GROW | BFREE_F_SEAL_WRITE)) != 0U) {
                return -22;
            }
            if ((vf->seals & (uint32_t)BFREE_F_SEAL_SEAL) != 0U) {
                return -1; /* EPERM: already sealed against sealing */
            }
            vf->seals |= add;
            return 0;
        }
    case 1034: /* F_GET_SEALS */
        {
            bfree_guest_vfile_t *vf = bfree_guest_vfile_from_open_fd(resolved, 0);

            if (!vf || !bfree_guest_vfile_is_memfd(vf)) {
                return -22;
            }
            return (long)vf->seals;
        }
    default:
        return 0;
    }
}

static long sys_linux_readlink(long dirfd, long path_ptr, long buf, long bufsiz)
{
    char path[256];
    char vname[64];
    const char *target;
    size_t n;
    size_t i;
    long path_err;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (strcmp(path, "/proc/self/exe") == 0 || strcmp(path, "/proc/1/exe") == 0) {
        target = g_guest_busybox_exe_path;
    } else if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) &&
               vname[0] != '\0') {
        bfree_guest_vfile_t *vf = bfree_guest_vfile_find_by_name(vname);

        if (!vf) {
            return -2;
        }
        if (!vf->is_symlink) {
            return -22; /* EINVAL: not a symlink */
        }
        target = (const char *)vf->data;
    } else {
        return -2;
    }
    n = 0;
    while (target[n] != '\0') {
        ++n;
    }
    if (buf == 0 || bufsiz <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if ((size_t)bufsiz <= n) {
        return -34;
    }
    for (i = 0; i < n; ++i) {
        ((char *)(uintptr_t)buf)[i] = target[i];
    }
    return (long)n;
}

static long sys_linux_setuid(long uid)
{
    g_guest_uid = (uint32_t)uid;
    g_guest_euid = (uint32_t)uid;
    return 0;
}
static long sys_linux_setgid(long gid)
{
    g_guest_gid = (uint32_t)gid;
    g_guest_egid = (uint32_t)gid;
    return 0;
}
static long sys_linux_getuid(void)
{
    return (long)g_guest_uid;
}

static long sys_linux_geteuid(void)
{
    return 0;
}

static long sys_linux_getgid(void)
{
    return 0;
}

static long sys_linux_getegid(void)
{
    return 0;
}

static uint32_t g_guest_groups[8];
static int g_guest_ngroups = 1; /* default: gid 0 */

static long sys_linux_getgroups(long size, long list)
{
    uint32_t *groups;
    int i;

    /* Single-user guest: root belongs only to gid 0 (or setgroups list). */
    if (g_guest_ngroups < 1) {
        g_guest_ngroups = 1;
        g_guest_groups[0] = 0;
    }
    if (size == 0) {
        return g_guest_ngroups;
    }
    if (size < 0) {
        return -22; /* EINVAL */
    }
    if (size < g_guest_ngroups) {
        return -22; /* EINVAL: buffer too small */
    }
    if (list == 0 || !bfree_user_ptr_mapped(list)) {
        return -14; /* EFAULT */
    }
    groups = (uint32_t *)(uintptr_t)list;
    for (i = 0; i < g_guest_ngroups; ++i) {
        groups[i] = g_guest_groups[i];
    }
    return g_guest_ngroups;
}

/* Linux 116: setgroups — soft accept up to 8 gids. */
static long sys_linux_setgroups(long size, long list)
{
    int i;

    if (size < 0) {
        return -22;
    }
    if (size > 8) {
        return -22;
    }
    if (size == 0) {
        g_guest_ngroups = 0;
        return 0;
    }
    if (list == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)list,
                                            (uint64_t)size * sizeof(uint32_t))) {
        return -14;
    }
    for (i = 0; i < (int)size; ++i) {
        g_guest_groups[i] = ((uint32_t *)(uintptr_t)list)[i];
    }
    g_guest_ngroups = (int)size;
    return 0;
}

typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} bfree_linux_iovec_t;

static int bfree_user_buf_mapped(uint64_t base, uint64_t len)
{
    if (len == 0) {
        return 1;
    }
    if (base == 0) {
        return 0;
    }
    return bfree_user_vaddr_mapped(base)
        && bfree_user_vaddr_mapped(base + len - 1U);
}

static long sys_linux_writev(long fd, long iov_ptr, long iovcnt)
{
    long total = 0;
    long i;

    if (iovcnt <= 0 || iovcnt > 1024) {
        return -22;
    }
    if (iov_ptr == 0 || !bfree_user_ptr_mapped(iov_ptr)) {
        return -14;
    }
    for (i = 0; i < iovcnt; ++i) {
        bfree_linux_iovec_t vec;
        uint64_t vec_addr = (uint64_t)(uintptr_t)(iov_ptr + i * (long)sizeof(vec));
        long chunk;

        if (!bfree_user_buf_mapped(vec_addr, sizeof(vec))) {
            return -14;
        }
        memcpy(&vec, (const void *)(uintptr_t)vec_addr, sizeof(vec));
        if (vec.iov_len == 0) {
            continue;
        }
        if (!bfree_user_buf_mapped(vec.iov_base, vec.iov_len)) {
            return -14;
        }
        chunk = sys_linux_write(fd, (long)vec.iov_base, (long)vec.iov_len);
        if (chunk < 0) {
            return total > 0 ? total : chunk;
        }
        total += chunk;
        if (chunk < (long)vec.iov_len) {
            break;
        }
    }
    return total;
}

static long sys_linux_readv(long fd, long iov_ptr, long iovcnt)
{
    long total = 0;
    long i;

    if (iovcnt <= 0 || iovcnt > 1024) {
        return -22; /* EINVAL */
    }
    if (iov_ptr == 0 || !bfree_user_ptr_mapped(iov_ptr)) {
        return -14; /* EFAULT */
    }
    for (i = 0; i < iovcnt; ++i) {
        bfree_linux_iovec_t vec;
        uint64_t vec_addr = (uint64_t)(uintptr_t)(iov_ptr + i * (long)sizeof(vec));
        long chunk;

        if (!bfree_user_buf_mapped(vec_addr, sizeof(vec))) {
            return -14;
        }
        memcpy(&vec, (const void *)(uintptr_t)vec_addr, sizeof(vec));
        if (vec.iov_len == 0) {
            continue;
        }
        if (!bfree_user_buf_mapped(vec.iov_base, vec.iov_len)) {
            return -14;
        }
        chunk = sys_linux_read(fd, (long)vec.iov_base, (long)vec.iov_len);
        if (chunk < 0) {
            return total > 0 ? total : chunk;
        }
        if (chunk == 0) {
            break; /* EOF */
        }
        total += chunk;
        if (chunk < (long)vec.iov_len) {
            break;
        }
    }
    return total;
}

static long sys_linux_rt_sigaction(long signum, long act, long oldact, long sigsetsize)
{
    typedef struct {
        void *sa_handler;
        unsigned long sa_flags;
        void *sa_restorer;
        unsigned long sa_mask;
    } bfree_k_sigaction_t;
    bfree_k_sigaction_t *ka;
    int sig = (int)signum;

    (void)sigsetsize;
    if (sig <= 0 || sig >= BFREE_NSIG || sig == 9 || sig == 19) {
        return -22;
    }
    if (oldact != 0) {
        if (!bfree_user_ptr_mapped(oldact)) {
            return -14;
        }
        ka = (bfree_k_sigaction_t *)(uintptr_t)oldact;
        if (g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
            ka->sa_handler = (void *)(uintptr_t)1;
        } else if (g_guest_sig_disp[sig] == BFREE_SIG_CATCH) {
            ka->sa_handler = g_guest_sig_handler[sig];
        } else {
            ka->sa_handler = (void *)0;
        }
        ka->sa_flags = g_guest_sig_flags[sig];
        ka->sa_restorer = g_guest_sig_restorer[sig];
        ka->sa_mask = (unsigned long)g_guest_sig_sa_mask[sig];
    }
    if (act != 0) {
        void *handler;
        if (!bfree_user_ptr_mapped(act)) {
            return -14;
        }
        ka = (bfree_k_sigaction_t *)(uintptr_t)act;
        handler = ka->sa_handler;
        g_guest_sig_flags[sig] = ka->sa_flags;
        g_guest_sig_restorer[sig] = ka->sa_restorer;
        g_guest_sig_sa_mask[sig] = (uint64_t)ka->sa_mask;
        g_guest_sig_handler[sig] = handler;
        if (handler == (void *)0) {
            g_guest_sig_disp[sig] = BFREE_SIG_DFL;
        } else if (handler == (void *)(uintptr_t)1) {
            g_guest_sig_disp[sig] = BFREE_SIG_IGN;
        } else {
            g_guest_sig_disp[sig] = BFREE_SIG_CATCH;
        }
    }
    return 0;
}

static long sys_linux_rt_sigprocmask(long how, long set, long oldset, long sigsetsize)
{
    uint64_t newmask;
    uint64_t old;

    (void)sigsetsize;
    old = g_guest_sig_mask;
    if (oldset != 0) {
        if (!bfree_user_ptr_mapped(oldset)) {
            return -14;
        }
        *(uint64_t *)(uintptr_t)oldset = old;
    }
    if (set == 0) {
        return 0;
    }
    if (!bfree_user_ptr_mapped(set)) {
        return -14;
    }
    newmask = *(uint64_t *)(uintptr_t)set;
    newmask &= ~((1ULL << 8) | (1ULL << 18));
    if (how == 0) { /* SIG_BLOCK */
        g_guest_sig_mask = old | newmask;
    } else if (how == 1) { /* SIG_UNBLOCK */
        g_guest_sig_mask = old & ~newmask;
    } else if (how == 2) { /* SIG_SETMASK */
        g_guest_sig_mask = newmask;
    } else {
        return -22;
    }
    return 0;
}

#define BFREE_LINUX_FIONREAD 0x541B
#define BFREE_LINUX_FIONBIO  0x5421
#define BFREE_LINUX_TCGETS   0x5401
#define BFREE_LINUX_TCSETS   0x5402
#define BFREE_LINUX_TIOCGWINSZ 0x5413
#define BFREE_LINUX_TIOCSWINSZ 0x5414

typedef struct {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
} bfree_guest_winsize_t;

static bfree_guest_winsize_t g_guest_winsize = { 24, 80, 0, 0 };

static long bfree_guest_fill_termios(long fd, long termios_ptr)
{
    bfree_termios_t *termios_p = (bfree_termios_t *)(uintptr_t)termios_ptr;

    if (termios_p == 0 || !bfree_user_ptr_mapped(termios_ptr)) {
        return -14;
    }
    if (fd < 0 || fd > 2) {
        return -9;
    }
    bfree_guest_tty_ensure_init();
    memcpy(termios_p, &g_guest_tty_termios, sizeof(*termios_p));
    return 0;
}

static long sys_linux_ioctl(long fd, long request, long arg)
{
    int *pending;
    int resolved = bfree_guest_fd_resolve((int)fd);

    if (bfree_guest_is_pipe_rd(resolved) && request == BFREE_LINUX_FIONREAD) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd(resolved);

        if (arg == 0 || !bfree_user_ptr_mapped(arg) || !ps) {
            return -14;
        }
        pending = (int *)(uintptr_t)arg;
        *pending = (int)ps->len;
        return 0;
    }
    if (request == BFREE_LINUX_FIONBIO) {
        int nb;

        if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
            return -14;
        }
        nb = *(const int *)(uintptr_t)arg;
        return sys_linux_fcntl(resolved, 4 /* F_SETFL */,
                               nb ? (long)BFREE_LINUX_O_NONBLOCK : 0L);
    }
    if (fd >= 0 && fd <= 2) {
        if (request == BFREE_LINUX_TCGETS) {
            return bfree_guest_fill_termios(fd, arg);
        }
        if (request == BFREE_LINUX_TCSETS) {
            return bfree_guest_tty_set_termios(arg);
        }
        if (request == BFREE_LINUX_TIOCGPGRP) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            *(int *)(uintptr_t)arg = g_guest_tty_pgrp;
            return 0;
        }
        if (request == BFREE_LINUX_TIOCSPGRP) {
            int pg;
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            pg = *(int *)(uintptr_t)arg;
            if (pg <= 0) {
                return -22;
            }
            /* fg: ash/busybox tcsetpgrp after setpgid — sync tty foreground. */
            g_guest_tty_pgrp = pg;
            return 0;
        }
        if (request == BFREE_LINUX_TIOCGWINSZ) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            *(bfree_guest_winsize_t *)(uintptr_t)arg = g_guest_winsize;
            return 0;
        }
        if (request == BFREE_LINUX_TIOCSWINSZ) {
            if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                return -14;
            }
            g_guest_winsize = *(const bfree_guest_winsize_t *)(uintptr_t)arg;
            return 0;
        }
    }
    {
        int pty_resolved = bfree_guest_fd_resolve((int)fd);
        int slot = bfree_pty_slot_from_fd(pty_resolved);
        if (slot >= 0) {
            if ((unsigned long)request == 0x80045430UL /* TIOCGPTN */) {
                if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                    return -14;
                }
                *(unsigned int *)(uintptr_t)arg = (unsigned int)slot;
                return 0;
            }
            return 0;
        }
        if (pty_resolved == (int)BFREE_GUEST_DEV_TTY_FD) {
            if (request == BFREE_LINUX_TIOCGPGRP) {
                if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                    return -14;
                }
                *(int *)(uintptr_t)arg = g_guest_tty_pgrp;
                return 0;
            }
            if (request == BFREE_LINUX_TIOCSPGRP) {
                int pg;
                if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                    return -14;
                }
                pg = *(int *)(uintptr_t)arg;
                if (pg <= 0) {
                    return -22;
                }
                g_guest_tty_pgrp = pg;
                return 0;
            }
            if (request == BFREE_LINUX_TIOCGWINSZ) {
                if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                    return -14;
                }
                *(bfree_guest_winsize_t *)(uintptr_t)arg = g_guest_winsize;
                return 0;
            }
            if (request == BFREE_LINUX_TIOCSWINSZ) {
                if (arg == 0 || !bfree_user_ptr_mapped(arg)) {
                    return -14;
                }
                g_guest_winsize = *(const bfree_guest_winsize_t *)(uintptr_t)arg;
                return 0;
            }
            return 0;
        }
    }
    (void)fd;
    (void)request;
    (void)arg;
    return -25; /* ENOTTY */
}

#define BFREE_LINUX_S_IFDIR 0040000U
#define BFREE_LINUX_S_IFCHR 0020000U
#define BFREE_LINUX_S_IFREG 0100000U
#define BFREE_LINUX_S_IFLNK 0120000U
#define BFREE_LINUX_S_IFIFO 0010000U

static long sys_linux_fchmod(long fd, long mode)
{
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;
    uint32_t type;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf) {
        return 0; /* non-vfile: accept like previous stub */
    }
    if (vf->is_dir) {
        type = BFREE_LINUX_S_IFDIR;
    } else if (vf->is_symlink) {
        type = BFREE_LINUX_S_IFLNK;
    } else {
        type = BFREE_LINUX_S_IFREG;
    }
    vf->mode = type | ((uint32_t)mode & 07777U);
    return 0;
}

static long sys_linux_chmod(long dirfd, long path_ptr, long mode)
{
    char path[192];
    char vname[64];
    bfree_guest_vfile_t *vf;
    long path_err;
    uint32_t type;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (strncmp(path, "/dev/shm/", 9) == 0 && path[9] != '\0') {
        char alt[192];
        size_t i;

        alt[0] = '/'; alt[1] = 't'; alt[2] = 'm'; alt[3] = 'p';
        alt[4] = '/'; alt[5] = 's'; alt[6] = 'h'; alt[7] = 'm';
        alt[8] = '/';
        for (i = 0; path[9 + i] != '\0' && i + 10U < sizeof(alt); ++i) {
            alt[9 + i] = path[9 + i];
        }
        alt[9 + i] = '\0';
        for (i = 0; alt[i] != '\0' && i + 1U < sizeof(path); ++i) {
            path[i] = alt[i];
        }
        path[i] = '\0';
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return 0; /* outside /tmp: keep stub success */
    }
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf) {
        return -2;
    }
    if (vf->is_dir) {
        type = BFREE_LINUX_S_IFDIR;
    } else if (vf->is_symlink) {
        type = BFREE_LINUX_S_IFLNK;
    } else {
        type = BFREE_LINUX_S_IFREG;
    }
    vf->mode = type | ((uint32_t)mode & 07777U);
    return 0;
}

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atim_sec;
    int64_t st_atim_nsec;
    int64_t st_mtim_sec;
    int64_t st_mtim_nsec;
    int64_t st_ctim_sec;
    int64_t st_ctim_nsec;
    int64_t __unused[3];
} bfree_linux_stat_t;

static long bfree_linux_stat_fill(long statbuf, uint32_t mode, int64_t size)
{
    bfree_linux_stat_t *st;

    if (statbuf == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)statbuf, sizeof(bfree_linux_stat_t))) {
        return -14;
    }
    st = (bfree_linux_stat_t *)(uintptr_t)statbuf;
    memset(st, 0, 144U);
    st->st_dev = 1ULL;
    st->st_ino = 1ULL;
    st->st_mode = mode;
    st->st_nlink = 1U;
    st->st_uid = 0U;
    st->st_gid = 0U;
    st->st_blksize = 4096;
    st->st_size = size;
    st->st_blocks = (size + 511) / 512;
    return 0;
}



typedef char bfree_linux_stat_size_ok[(sizeof(bfree_linux_stat_t) == 144U) ? 1 : -1];



/* /tmp vfiles need distinct inodes: tar compares (st_dev, st_ino) of the
 * archive against each input file and skips "the archive itself" when every
 * vfile reports ino 1 (observed: "tar: b2f: file is the archive; skipping",
 * producing an empty archive). */
static long bfree_linux_stat_fill_vfile(long statbuf, const bfree_guest_vfile_t *vf)
{
    uint32_t mode;
    int64_t size;
    long ret;

    if (vf->is_dir) {
        size = 4096;
        mode = (vf->mode != 0U) ? vf->mode : (BFREE_LINUX_S_IFDIR | 0755U);
    } else if (vf->is_symlink) {
        size = (int64_t)vf->len;
        mode = (vf->mode != 0U) ? vf->mode : (BFREE_LINUX_S_IFLNK | 0777U);
    } else {
        size = (int64_t)vf->len;
        mode = (vf->mode != 0U) ? vf->mode : (BFREE_LINUX_S_IFREG | 0644U);
    }
    ret = bfree_linux_stat_fill(statbuf, mode, size);
    if (ret == 0) {
        bfree_linux_stat_t *st = (bfree_linux_stat_t *)(uintptr_t)statbuf;
        st->st_ino = 100ULL + (uint64_t)(vf - g_guest_vfiles);
        st->st_nlink = (vf->nlink > 0) ? (uint64_t)vf->nlink : 1ULL;
        st->st_uid = vf->uid;
        st->st_gid = vf->gid;
        st->st_atim_sec = vf->atime_sec;
        st->st_atim_nsec = vf->atime_nsec;
        st->st_mtim_sec = vf->mtime_sec;
        st->st_mtim_nsec = vf->mtime_nsec;
        st->st_ctim_sec = vf->mtime_sec;
        st->st_ctim_nsec = vf->mtime_nsec;
    }
    return ret;
}

static long bfree_linux_stat_for_path(const char *path, long statbuf)
{
    size_t exe_len;

    if (bfree_linux_path_is_dot_or_slash(path)) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/dev/null") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFCHR | 0666U, 0);
    }
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' &&
        (path[4] == '/' || path[4] == '\0')) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/proc/self/exe") == 0 || strcmp(path, "/proc/1/exe") == 0) {
        exe_len = 0;
        while (g_guest_busybox_exe_path[exe_len] != '\0') {
            ++exe_len;
        }
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFLNK | 0777U, (int64_t)exe_len);
    }
    if (strcmp(path, "/proc") == 0 ||
        strcmp(path, "/proc/self") == 0 ||
        strcmp(path, "/proc/1") == 0 ||
        strcmp(path, "/proc/2") == 0 ||
        strcmp(path, "/proc/1/") == 0 ||
        strcmp(path, "/proc/2/") == 0 ||
        strcmp(path, "/proc/self/") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0555U, 4096);
    }
    if (strcmp(path, "/proc/self/maps") == 0 || strcmp(path, "/proc/1/maps") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)bfree_guest_cstr_len(g_guest_proc_maps));
    }
    if (strcmp(path, "/proc/self/stat") == 0 || strcmp(path, "/proc/1/stat") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid_stat) - 1U));
    }
    if (strcmp(path, "/proc/2/stat") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid2_stat) - 1U));
    }
    if (strcmp(path, "/proc/self/cmdline") == 0 || strcmp(path, "/proc/1/cmdline") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_cmdline) - 1U));
    }
    if (strcmp(path, "/proc/2/cmdline") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid2_cmdline) - 1U));
    }
    if (strcmp(path, "/proc/self/status") == 0 || strcmp(path, "/proc/1/status") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_status) - 1U));
    }
    if (strcmp(path, "/proc/meminfo") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_meminfo) - 1U));
    }
    if (strcmp(path, "/proc/uptime") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_uptime_file) - 1U));
    }
    if (strcmp(path, "/proc/loadavg") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_loadavg) - 1U));
    }
    if (strcmp(path, "/proc/stat") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_cpustat) - 1U));
    }
    if (strcmp(path, "/proc/mounts") == 0 || strcmp(path, "/proc/self/mounts") == 0) {
        bfree_guest_proc_mounts_ensure();
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)g_guest_proc_mounts_len);
    }
    if (path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' &&
        path[4] == 'c' && (path[5] == '/' || path[5] == '\0')) {
        return -2;
    }
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'n' && path[4] == '\0') {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'n' &&
        path[4] == '/' && path[5] != '\0') {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 493048);
    }
    if (strcmp(path, "/etc") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/root") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0700U, 4096);
    }
    if (strcmp(path, "/etc/passwd") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_passwd) - 1U));
    }
    if (strcmp(path, "/etc/group") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_group) - 1U));
    }
    if (strcmp(path, "/etc/profile") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_profile) - 1U));
    }
    if (strcmp(path, "/etc/motd") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_motd) - 1U));
    }
    if (strcmp(path, "/etc/hosts") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_hosts) - 1U));
    }
    if (strcmp(path, "/usr") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/usr/bin") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'r' &&
        path[4] == '/' && path[5] == 'b' && path[6] == 'i' && path[7] == 'n' &&
        path[8] == '/' && path[9] != '\0') {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 493048);
    }
    if (strcmp(path, "/var") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, g_guest_busybox_exe_path) == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 167424);
    }
    if (strcmp(path, "/tmp") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 01777U, 4096);
    }
    if (strcmp(path, "/home") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/persist") == 0) {
        bfree_persist_load_once();
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    {
        char vname[64];

        if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) && vname[0] != '\0') {
            bfree_guest_vfile_t *vf = bfree_guest_vfile_find_by_name(vname);

            if (vf) {
                return bfree_linux_stat_fill_vfile(statbuf, vf);
            }
            /* Directory markers (home/var/persist) may lack a vnode until mkdir. */
            if (strcmp(vname, "home") == 0 || strcmp(vname, "var") == 0 ||
                strcmp(vname, "persist") == 0) {
                if (strcmp(vname, "persist") == 0) {
                    bfree_persist_load_once();
                }
                return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
            }
            return -2;
        }
    }
    return -2;
}

static long sys_linux_statx(long dfd, long path_ptr, long flags, long mask, long statxbuf)
{
    /* Was hardcoded to ~46MB for every path (Qt probe leftover), which made
     * tar/musl-statx think /tmp files were huge → "short read" after copying
     * only the real content. Fill from the same path rules as stat(). */
    typedef struct {
        int64_t tv_sec;
        uint32_t tv_nsec;
        int32_t __reserved;
    } bfree_statx_ts_t;
    struct bfree_guest_statx {
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
        uint64_t stx_attributes_mask;
        bfree_statx_ts_t stx_atime;
        bfree_statx_ts_t stx_btime;
        bfree_statx_ts_t stx_ctime;
        bfree_statx_ts_t stx_mtime;
    } *st;
    char path[256];
    bfree_linux_stat_t tmp;
    long ret;
    long path_err;
    uint64_t us;
    long sec;
    uint32_t nsec;
#define BFREE_AT_EMPTY_PATH 0x1000L

    (void)mask;
    if (statxbuf == 0 || !bfree_user_ptr_mapped(statxbuf)) {
        return -14;
    }
    st = (struct bfree_guest_statx *)(uintptr_t)statxbuf;
    {
        uint8_t *p = (uint8_t *)st;
        size_t i;
        for (i = 0; i < 256U; ++i) {
            p[i] = 0;
        }
    }
    if (path_ptr == 0) {
        return -14;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    /* AT_EMPTY_PATH: empty pathname → operate on dirfd itself. */
    if (path[0] == '\0' && (flags & BFREE_AT_EMPTY_PATH) != 0L) {
        ret = sys_linux_fstat(dfd, (long)(uintptr_t)&tmp);
    } else {
        path_err = bfree_guest_path_at(dfd, path, sizeof(path));
        if (path_err != 0) {
            return path_err;
        }
        ret = bfree_linux_stat_for_path(path, (long)(uintptr_t)&tmp);
    }
    if (ret != 0) {
        return ret;
    }
    us = knl_get_current_time();
    sec = (long)(us / 1000000ULL);
    nsec = (uint32_t)((us % 1000000ULL) * 1000ULL);
    st->stx_mask = 0x000007ffU; /* STATX_BASIC_STATS */
    st->stx_blksize = (uint32_t)tmp.st_blksize;
    st->stx_nlink = (uint32_t)tmp.st_nlink;
    st->stx_uid = tmp.st_uid;
    st->stx_gid = tmp.st_gid;
    st->stx_mode = (uint16_t)tmp.st_mode;
    st->stx_ino = tmp.st_ino;
    st->stx_size = (uint64_t)tmp.st_size;
    st->stx_blocks = (uint64_t)tmp.st_blocks;
    st->stx_atime.tv_sec = sec;
    st->stx_atime.tv_nsec = nsec;
    st->stx_ctime.tv_sec = sec;
    st->stx_ctime.tv_nsec = nsec;
    st->stx_mtime.tv_sec = sec;
    st->stx_mtime.tv_nsec = nsec;
    return 0;
}

static long sys_linux_stat(long path_ptr, long statbuf)
{
    char path[256];

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    bfree_guest_path_absolutize(path, sizeof(path));
    return bfree_linux_stat_for_path(path, statbuf);
}

static long sys_linux_fstat(long fd, long statbuf)
{
    bfree_guest_vfile_t *vf;
    int target;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, 0);
    if (vf) {
        return bfree_linux_stat_fill_vfile(statbuf, vf);
    }
    target = bfree_guest_open_target((int)fd, 0);
    if (target == (int)BFREE_GUEST_ROOT_DIR_FD || target == (int)BFREE_GUEST_TMP_DIR_FD ||
        target == (int)BFREE_GUEST_BIN_DIR_FD || target == (int)BFREE_GUEST_USR_DIR_FD ||
        target == (int)BFREE_GUEST_VAR_DIR_FD || target == (int)BFREE_GUEST_HOME_DIR_FD ||
        target == (int)BFREE_GUEST_PERSIST_DIR_FD ||
        target == (int)BFREE_GUEST_PROC_DIR_FD ||
        target == (int)BFREE_GUEST_PROC_PID_DIR_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    fd = (long)target;
    if (fd == (long)BFREE_GUEST_BUSYBOX_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 493048);
    }
    if (fd == (long)BFREE_GUEST_PASSWD_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_passwd) - 1U));
    }
    if (fd == (long)BFREE_GUEST_GROUP_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_group) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROFILE_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_profile) - 1U));
    }
    if (fd == (long)BFREE_GUEST_MOTD_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_motd) - 1U));
    }
    if (fd == (long)BFREE_GUEST_HOSTS_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_hosts) - 1U));
    }
    if (fd == (long)BFREE_GUEST_RESOLV_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0644U,
            (int64_t)(sizeof(g_guest_etc_resolv) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_MAPS_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)bfree_guest_cstr_len(g_guest_proc_maps));
    }
    if (fd == (long)BFREE_GUEST_PROC_PIDSTAT_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid_stat) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_CMDLINE_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_cmdline) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_STATUS_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_status) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_MEMINFO_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_meminfo) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_UPTIME_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_uptime_file) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_LOADAVG_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_loadavg) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_CPUSTAT_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_cpustat) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_MOUNTS_FD) {
        bfree_guest_proc_mounts_ensure();
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)g_guest_proc_mounts_len);
    }
    if (fd == (long)BFREE_GUEST_PROC_PID2STAT_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid2_stat) - 1U));
    }
    if (fd == (long)BFREE_GUEST_PROC_PID2CMDLINE_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_pid2_cmdline) - 1U));
    }
    if (fd >= 0 && fd <= 2) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFCHR | 0666U, 0);
    }
    return -9;
}

static long sys_linux_lstat(long path_ptr, long statbuf)
{
    return sys_linux_stat(path_ptr, statbuf);
}

static char g_guest_hostname[64] = "bfree";
static char g_guest_domainname[64] = "(none)";

static long sys_linux_gethostname(long buf, long len)
{
    size_t n;
    size_t i;

    if (buf == 0 || len <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    n = 0;
    while (g_guest_hostname[n] != '\0' && n < sizeof(g_guest_hostname) - 1U) {
        ++n;
    }
    if ((size_t)len <= n) {
        return -12;
    }
    for (i = 0; i < n; ++i) {
        ((char *)(uintptr_t)buf)[i] = g_guest_hostname[i];
    }
    ((char *)(uintptr_t)buf)[n] = '\0';
    return 0;
}

/* Linux 170: sethostname — was wrongly wired to gethostname. */
static long sys_linux_sethostname(long name_ptr, long len)
{
    size_t i;

    if (len < 0) {
        return -22;
    }
    if (len >= (long)sizeof(g_guest_hostname)) {
        return -22;
    }
    if (len > 0 && (name_ptr == 0 ||
                    !bfree_user_buf_mapped((uint64_t)(uintptr_t)name_ptr,
                                          (uint64_t)len))) {
        return -14;
    }
    for (i = 0; i < (size_t)len; ++i) {
        g_guest_hostname[i] = ((const char *)(uintptr_t)name_ptr)[i];
    }
    g_guest_hostname[len] = '\0';
    return 0;
}

/* Linux 171: setdomainname */
static long sys_linux_setdomainname(long name_ptr, long len)
{
    size_t i;

    if (len < 0) {
        return -22;
    }
    if (len >= (long)sizeof(g_guest_domainname)) {
        return -22;
    }
    if (len > 0 && (name_ptr == 0 ||
                    !bfree_user_buf_mapped((uint64_t)(uintptr_t)name_ptr,
                                          (uint64_t)len))) {
        return -14;
    }
    for (i = 0; i < (size_t)len; ++i) {
        g_guest_domainname[i] = ((const char *)(uintptr_t)name_ptr)[i];
    }
    g_guest_domainname[len] = '\0';
    return 0;
}

static long sys_linux_newfstatat(long dirfd, long path_ptr, long statbuf, long flags)
{
    char path[256];
    long path_err;

    (void)flags;
    if (statbuf == 0 || !bfree_user_ptr_mapped(statbuf)) {
        return -14;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    return bfree_linux_stat_for_path(path, statbuf);
}

static long sys_linux_getdents64(long fd, long dirp, long count)
{
    static const char *const k_root_names[] = {
        ".", "..", "bin", "dev", "etc", "home", "persist", "proc", "root", "tmp", "usr", "var", "busybox.elf"
    };
    static const char *const k_bin_names[] = {
        ".", "..", "sh", "busybox", "echo", "cat", "ls", "grep", "mkdir", "rm", "cp", "mv",
        "tee", "mktemp", "base64", "sha256sum", "nslookup"
    };
    static const char *const k_usr_names[] = {
        ".", "..", "bin"
    };
    static const char *const k_var_names[] = {
        ".", ".."
    };
    static const char *const k_proc_names[] = {
        ".", "..", "1", "2", "self", "meminfo", "uptime", "loadavg", "stat", "mounts"
    };
    static const char *const k_proc_pid_names[] = {
        ".", "..", "stat", "cmdline", "status", "maps", "exe"
    };
    typedef struct __attribute__((packed)) {
        uint64_t d_ino;
        int64_t d_off;
        unsigned short d_reclen;
        unsigned char d_type;
        char d_name[256];
    } bfree_linux_dirent64_t;
    bfree_linux_dirent64_t ent;
    const char *name;
    unsigned short reclen;
    size_t nlen;
    unsigned char dtype;
    unsigned name_count;
    uint8_t *out;
    unsigned i;
    bfree_guest_ofd_t *ofd;
    int target;
    unsigned dir_idx;
    const char *tmp_parent;
    static char tmp_child[48];
    bfree_guest_vfile_t *dvf;

    fd = bfree_guest_fd_resolve((int)fd);
    if (dirp == 0 || count <= 0 || !bfree_user_ptr_mapped(dirp)) {
        return -14;
    }
    target = bfree_guest_open_target((int)fd, &ofd);
    if (!ofd) {
        return -9; /* directory streams require an open-file description */
    }
    dir_idx = (unsigned)ofd->pos;
    dvf = bfree_guest_vfile_from_fd(target);
    tmp_parent = 0;

    if (target == (int)BFREE_GUEST_ROOT_DIR_FD) {
        name_count = (unsigned)(sizeof(k_root_names) / sizeof(k_root_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_root_names[dir_idx];
        dtype = (unsigned char)((strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
                               strcmp(name, "bin") == 0 || strcmp(name, "dev") == 0 ||
                               strcmp(name, "etc") == 0 || strcmp(name, "home") == 0 ||
                               strcmp(name, "persist") == 0 || strcmp(name, "proc") == 0 ||
                               strcmp(name, "root") == 0 || strcmp(name, "tmp") == 0 ||
                               strcmp(name, "usr") == 0 || strcmp(name, "var") == 0) ? 4U : 8U);
    } else if (target == (int)BFREE_GUEST_BIN_DIR_FD) {
        name_count = (unsigned)(sizeof(k_bin_names) / sizeof(k_bin_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_bin_names[dir_idx];
        dtype = (unsigned char)((strcmp(name, ".") == 0 || strcmp(name, "..") == 0) ? 4U : 8U);
    } else if (target == (int)BFREE_GUEST_USR_DIR_FD) {
        name_count = (unsigned)(sizeof(k_usr_names) / sizeof(k_usr_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_usr_names[dir_idx];
        dtype = (unsigned char)((strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
                               strcmp(name, "bin") == 0) ? 4U : 8U);
    } else if (target == (int)BFREE_GUEST_VAR_DIR_FD) {
        name_count = (unsigned)(sizeof(k_var_names) / sizeof(k_var_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_var_names[dir_idx];
        dtype = (unsigned char)((strcmp(name, ".") == 0 || strcmp(name, "..") == 0) ? 4U : 8U);
    } else if (target == (int)BFREE_GUEST_PROC_DIR_FD) {
        name_count = (unsigned)(sizeof(k_proc_names) / sizeof(k_proc_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_proc_names[dir_idx];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            strcmp(name, "1") == 0 || strcmp(name, "2") == 0) {
            dtype = 4U;
        } else if (strcmp(name, "self") == 0) {
            dtype = 10U;
        } else {
            dtype = 8U;
        }
    } else if (target == (int)BFREE_GUEST_PROC_PID_DIR_FD) {
        name_count = (unsigned)(sizeof(k_proc_pid_names) / sizeof(k_proc_pid_names[0]));
        if (dir_idx >= name_count) {
            return 0;
        }
        name = k_proc_pid_names[dir_idx];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            dtype = 4U;
        } else if (strcmp(name, "exe") == 0) {
            dtype = 10U;
        } else {
            dtype = 8U;
        }
    } else if (target == (int)BFREE_GUEST_TMP_DIR_FD ||
               target == (int)BFREE_GUEST_HOME_DIR_FD ||
               target == (int)BFREE_GUEST_PERSIST_DIR_FD ||
               (dvf && dvf->is_dir)) {
        if (target == (int)BFREE_GUEST_HOME_DIR_FD) {
            tmp_parent = "home";
        } else if (target == (int)BFREE_GUEST_PERSIST_DIR_FD) {
            bfree_persist_load_once();
            tmp_parent = "persist";
        } else {
            tmp_parent = (dvf && dvf->is_dir) ? dvf->name : "";
        }
        name_count = 2U;
        for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
            if (!g_guest_vfiles[i].used || g_guest_vfiles[i].orphaned) {
                continue;
            }
            if (tmp_parent[0] == '\0' &&
                bfree_guest_tmp_root_skip_ns(g_guest_vfiles[i].name)) {
                continue;
            }
            if (bfree_guest_tmp_child_basename(g_guest_vfiles[i].name, tmp_parent,
                    tmp_child, sizeof(tmp_child))) {
                ++name_count;
            }
        }
        if (dir_idx >= name_count) {
            return 0;
        }
        if (dir_idx == 0) {
            name = ".";
            dtype = 4U;
        } else if (dir_idx == 1) {
            name = "..";
            dtype = 4U;
        } else {
            unsigned want = dir_idx - 2U;
            unsigned seen = 0;

            name = 0;
            dtype = 8U;
            for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
                if (!g_guest_vfiles[i].used || g_guest_vfiles[i].orphaned) {
                    continue;
                }
                if (tmp_parent[0] == '\0' &&
                    bfree_guest_tmp_root_skip_ns(g_guest_vfiles[i].name)) {
                    continue;
                }
                if (!bfree_guest_tmp_child_basename(g_guest_vfiles[i].name, tmp_parent,
                        tmp_child, sizeof(tmp_child))) {
                    continue;
                }
                if (seen == want) {
                    name = tmp_child;
                    if (g_guest_vfiles[i].is_dir) {
                        dtype = 4U;
                    } else if (g_guest_vfiles[i].is_symlink) {
                        dtype = 10U;
                    } else {
                        dtype = 8U;
                    }
                    break;
                }
                ++seen;
            }
            if (!name) {
                return 0;
            }
        }
    } else {
        return -9;
    }

    nlen = 0;
    while (name[nlen] != '\0') {
        ++nlen;
    }
    if (g_getdents_legacy) {
        /* linux_dirent: ino, off, reclen, name[], then d_type as last byte. */
        reclen = (unsigned short)((8 + 8 + 2 + nlen + 1U + 1U + 7U) & ~7U);
        if ((long)reclen > count) {
            return 0;
        }
        out = (uint8_t *)(uintptr_t)dirp;
        memset(out, 0, reclen);
        *(uint64_t *)(void *)out = (uint64_t)dir_idx + 1ULL;
        ofd->dir_cookie += (uint64_t)reclen;
        *(int64_t *)(void *)(out + 8) = (int64_t)ofd->dir_cookie;
        *(unsigned short *)(void *)(out + 16) = reclen;
        for (i = 0; i < nlen; ++i) {
            out[18 + i] = (uint8_t)name[i];
        }
        out[reclen - 1U] = dtype;
        ofd->pos = (size_t)(dir_idx + 1U);
        return (long)reclen;
    }
    reclen = (unsigned short)((19 + nlen + 1U + 7U) & ~7U);
    if ((long)reclen > count) {
        return 0;
    }
    memset(&ent, 0, sizeof(ent));
    ent.d_ino = (uint64_t)dir_idx + 1ULL;
    ofd->dir_cookie += (uint64_t)reclen;
    ent.d_off = (int64_t)ofd->dir_cookie;
    ent.d_reclen = reclen;
    ent.d_type = dtype;
    for (i = 0; i < nlen; ++i) {
        ent.d_name[i] = name[i];
    }
    out = (uint8_t *)(uintptr_t)dirp;
    for (i = 0; i < (size_t)reclen; ++i) {
        out[i] = ((const uint8_t *)&ent)[i];
    }
    ofd->pos = (size_t)(dir_idx + 1U);
    return (long)reclen;
}

static long sys_linux_getcwd(long buf, long size)
{
    size_t n = 0;
    size_t i;
    char *dst;

    if (buf == 0 || size == 0) {
        return -14;
    }
    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)buf)) {
        return -14;
    }
    while (g_guest_cwd[n] != '\0') {
        ++n;
    }
    if (n == 0) {
        g_guest_cwd[0] = '/';
        g_guest_cwd[1] = '\0';
        n = 1;
    }
    if ((size_t)size < n + 1U) {
        return -34; /* ERANGE */
    }
    dst = (char *)(uintptr_t)buf;
    for (i = 0; i < n; ++i) {
        dst[i] = g_guest_cwd[i];
    }
    dst[n] = '\0';
    return (long)(n + 1U);
}

static long sys_linux_uname(long buf)
{
    typedef struct {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    } bfree_uname_t;
    bfree_uname_t *u;

    if (buf == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)buf)) {
        return -14;
    }
    u = (bfree_uname_t *)(uintptr_t)buf;
    memset(u, 0, sizeof(*u));
    {
        static const char *fields[] = {"B-Free", 0, "0.1", "guest", "x86_64"};
        char *dsts[] = {u->sysname, u->nodename, u->release, u->version, u->machine};
        int i;
        for (i = 0; i < 5; ++i) {
            size_t j = 0;
            const char *src = (i == 1) ? g_guest_hostname : fields[i];
            while (src[j] != '\0' && j < 64U) {
                dsts[i][j] = src[j];
                ++j;
            }
        }
        bfree_copy_cstr(u->domainname, sizeof(u->domainname), g_guest_domainname);
    }
    return 0;
}

static long sys_linux_lseek(long fd, long offset, long whence)
{
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;
    size_t *offp = 0;
    size_t total = 0;
    long cur;
    long next;
    int target;
    int raw_fd = (int)fd;

    fd = bfree_guest_fd_resolve((int)fd);
    /*
     * Do NOT treat ofd->target == ROOT/TMP/... magic as ESPIPE: those values
     * overlap VFILE_FD_BASE+slot (e.g. old slot 16 == 0x3720 == ROOT_DIR_FD).
     * Directory-ness comes from vf->is_dir or a true directory OFD target when
     * there is no vfile slot behind it.
     */
    target = bfree_guest_open_target((int)fd, &ofd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (vf) {
        if (vf->is_dir) {
            return -29;
        }
        offp = ofd ? &ofd->pos : &vf->pos;
        total = vf->len;
    } else if (ofd) {
        /* OFD without resolvable vfile (e.g. target cleared): still allow seek. */
        offp = &ofd->pos;
        total = 0;
    } else if (target == (int)BFREE_GUEST_ROOT_DIR_FD || target == (int)BFREE_GUEST_TMP_DIR_FD ||
               target == (int)BFREE_GUEST_BIN_DIR_FD || target == (int)BFREE_GUEST_USR_DIR_FD ||
               target == (int)BFREE_GUEST_VAR_DIR_FD || target == (int)BFREE_GUEST_HOME_DIR_FD ||
               target == (int)BFREE_GUEST_PERSIST_DIR_FD ||
               target == (int)BFREE_GUEST_PROC_DIR_FD ||
               target == (int)BFREE_GUEST_PROC_PID_DIR_FD ||
               target == (int)BFREE_GUEST_PTS_DIR_FD) {
        return -29; /* ESPIPE */
    } else if (fd == (long)BFREE_GUEST_PASSWD_FD) {
        offp = &g_guest_etc_passwd_off;
        total = sizeof(g_guest_etc_passwd) - 1U;
    } else if (fd == (long)BFREE_GUEST_GROUP_FD) {
        offp = &g_guest_etc_group_off;
        total = sizeof(g_guest_etc_group) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROFILE_FD) {
        offp = &g_guest_etc_profile_off;
        total = sizeof(g_guest_etc_profile) - 1U;
    } else if (fd == (long)BFREE_GUEST_MOTD_FD) {
        offp = &g_guest_etc_motd_off;
        total = sizeof(g_guest_etc_motd) - 1U;
    } else if (fd == (long)BFREE_GUEST_HOSTS_FD) {
        offp = &g_guest_etc_hosts_off;
        total = sizeof(g_guest_etc_hosts) - 1U;
    } else if (fd == (long)BFREE_GUEST_RESOLV_FD) {
        offp = &g_guest_etc_resolv_off;
        total = sizeof(g_guest_etc_resolv) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_MAPS_FD) {
        offp = &g_guest_proc_maps_off;
        total = bfree_guest_cstr_len(g_guest_proc_maps);
    } else if (fd == (long)BFREE_GUEST_PROC_PIDSTAT_FD) {
        offp = &g_guest_proc_pid_stat_off;
        total = sizeof(g_guest_proc_pid_stat) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_CMDLINE_FD) {
        offp = &g_guest_proc_cmdline_off;
        total = sizeof(g_guest_proc_cmdline) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_STATUS_FD) {
        offp = &g_guest_proc_status_off;
        total = sizeof(g_guest_proc_status) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_MEMINFO_FD) {
        offp = &g_guest_proc_meminfo_off;
        total = sizeof(g_guest_proc_meminfo) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_UPTIME_FD) {
        offp = &g_guest_proc_uptime_off;
        total = sizeof(g_guest_proc_uptime_file) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_LOADAVG_FD) {
        offp = &g_guest_proc_loadavg_off;
        total = sizeof(g_guest_proc_loadavg) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_CPUSTAT_FD) {
        offp = &g_guest_proc_cpustat_off;
        total = sizeof(g_guest_proc_cpustat) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_MOUNTS_FD) {
        bfree_guest_proc_mounts_ensure();
        offp = &g_guest_proc_mounts_off;
        total = g_guest_proc_mounts_len;
    } else if (fd == (long)BFREE_GUEST_PROC_PID2STAT_FD) {
        offp = &g_guest_proc_pid2_stat_off;
        total = sizeof(g_guest_proc_pid2_stat) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_PID2CMDLINE_FD) {
        offp = &g_guest_proc_pid2_cmdline_off;
        total = sizeof(g_guest_proc_pid2_cmdline) - 1U;
    } else if (bfree_guest_pipe_slot_from_magic((int)fd) >= 0) {
        return -29; /* ESPIPE */
    } else {
        /* Last resort: any other open fd is seekable (fixes tmpfile edge cases). */
        offp = bfree_guest_seek_pos_for(raw_fd >= 0 ? raw_fd : (int)fd);
        if (!offp) {
            offp = bfree_guest_seek_pos_for((int)fd);
        }
        if (!offp) {
            return -29;
        }
        total = 0;
    }

    cur = (long)(*offp);
    if (whence == 0) {
        next = offset;
    } else if (whence == 1) {
        next = cur + offset;
    } else if (whence == 2) {
        next = (long)total + offset;
    } else {
        return -22;
    }
    if (next < 0) {
        return -22;
    }
    /* Linux allows seeking past EOF; do not clamp to file size. */
    *offp = (size_t)next;
    return next;
}

static long sys_linux_mkdir(long dirfd, long path_ptr, long mode)
{
    char path[256];
    char vname[64];
    int fd;
    bfree_guest_vfile_t *vf;
    long path_err;

    (void)mode;
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (strcmp(path, "/tmp") == 0) {
        return -17; /* EEXIST */
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return -30; /* EROFS */
    }
    if (bfree_guest_vfile_find_by_name(vname)) {
        return -17; /* EEXIST */
    }
    if (!bfree_guest_tmp_parents_exist(vname)) {
        return -2; /* ENOENT */
    }
    fd = bfree_guest_vfile_alloc_slot(vname, 1);
    if (fd < 0) {
        return -28; /* ENOSPC */
    }
    vf = bfree_guest_vfile_from_fd(fd);
    if (!vf) {
        return -5;
    }
    vf->is_dir = 1;
    vf->is_symlink = 0;
    vf->len = 0;
    vf->pos = 0;
    return 0;
}

static long sys_linux_rmdir(long dirfd, long path_ptr)
{
    char path[256];
    char vname[64];
    bfree_guest_vfile_t *vf;
    long path_err;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (strcmp(path, "/tmp") == 0) {
        return -16; /* EBUSY */
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return -30;
    }
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf) {
        return -2;
    }
    if (!vf->is_dir) {
        return -20; /* ENOTDIR */
    }
    if (bfree_guest_tmp_has_children(vname)) {
        return -39; /* ENOTEMPTY */
    }
    vf->name[0] = '\0';
    if (vf->open_refs > 0) {
        vf->orphaned = 1;
        return 0;
    }
    bfree_guest_vfile_clear_slot(vf);
    return 0;
}

static long sys_linux_unlink(long dirfd, long path_ptr)
{
    char path[256];
    char vname[64];
    bfree_guest_vfile_t *vf;
    long path_err;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    /* musl shm_unlink → unlink("/dev/shm/<name>"); same map as open. */
    if (strncmp(path, "/dev/shm/", 9) == 0 && path[9] != '\0') {
        char alt[192];
        size_t i;

        alt[0] = '/';
        alt[1] = 't';
        alt[2] = 'm';
        alt[3] = 'p';
        alt[4] = '/';
        alt[5] = 's';
        alt[6] = 'h';
        alt[7] = 'm';
        alt[8] = '/';
        for (i = 0; path[9 + i] != '\0' && i + 10U < sizeof(alt); ++i) {
            alt[9 + i] = path[9 + i];
        }
        alt[9 + i] = '\0';
        for (i = 0; alt[i] != '\0' && i + 1U < sizeof(path); ++i) {
            path[i] = alt[i];
        }
        path[i] = '\0';
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) || vname[0] == '\0') {
        return -30;
    }
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf) {
        return -2;
    }
    if (vf->is_dir) {
        return -21; /* EISDIR */
    }
    bfree_inotify_notify_vname(vname, BFREE_IN_DELETE);
    {
        int was_persist = strncmp(vname, "persist/", 8) == 0;

        /* Remove this directory entry (primary name or alias). */
        if (strcmp(vf->name, vname) == 0) {
            vf->name[0] = '\0';
        } else {
            (void)bfree_guest_alias_remove_name(vname);
        }
        if (vf->nlink > 0) {
            vf->nlink--;
        }
        if (vf->nlink > 0) {
            if (was_persist) {
                bfree_persist_flush_all();
            }
            return 0; /* other hard links remain */
        }
        if (vf->open_refs > 0) {
            vf->orphaned = 1;
        } else {
            bfree_guest_vfile_clear_slot(vf);
        }
        if (was_persist) {
            bfree_persist_flush_all();
        }
    }
    return 0;
}

static int bfree_guest_tmp_rename_descendants(const char *oldname, const char *newname)
{
    size_t oldn;
    size_t newn;
    int i;

    oldn = strlen(oldname);
    newn = strlen(newname);
    for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
        char rebuilt[48];
        const char *rest;
        size_t restn;
        size_t j;

        if (!g_guest_vfiles[i].used) {
            continue;
        }
        if (strncmp(g_guest_vfiles[i].name, oldname, oldn) != 0 ||
            g_guest_vfiles[i].name[oldn] != '/') {
            continue;
        }
        rest = g_guest_vfiles[i].name + oldn;
        restn = strlen(rest);
        if (newn + restn + 1U > sizeof(rebuilt)) {
            return -36; /* ENAMETOOLONG */
        }
        for (j = 0; j < newn; ++j) {
            rebuilt[j] = newname[j];
        }
        for (j = 0; j < restn; ++j) {
            rebuilt[newn + j] = rest[j];
        }
        rebuilt[newn + restn] = '\0';
        for (j = 0; j <= newn + restn; ++j) {
            g_guest_vfiles[i].name[j] = rebuilt[j];
        }
    }
    return 0;
}

/* renameat2 flags */
#define BFREE_RENAME_NOREPLACE 1
#define BFREE_RENAME_EXCHANGE  2

static long sys_linux_rename(long olddirfd, long old_ptr, long newdirfd,
                             long new_ptr, long flags)
{
    char oldp[256];
    char newp[256];
    char oldname[64];
    char newname[64];
    bfree_guest_vfile_t *src;
    bfree_guest_vfile_t *dst;
    size_t n;
    int rc;
    long path_err;

    if ((flags & ~(long)(BFREE_RENAME_NOREPLACE | BFREE_RENAME_EXCHANGE)) != 0) {
        return -22; /* EINVAL: RENAME_WHITEOUT and friends unsupported */
    }
    if ((flags & BFREE_RENAME_NOREPLACE) != 0 &&
        (flags & BFREE_RENAME_EXCHANGE) != 0) {
        return -22;
    }
    if (copy_user_cstr(old_ptr, oldp, sizeof(oldp)) != 0 ||
        copy_user_cstr(new_ptr, newp, sizeof(newp)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(olddirfd, oldp, sizeof(oldp));
    if (path_err != 0) {
        return path_err;
    }
    path_err = bfree_guest_path_at(newdirfd, newp, sizeof(newp));
    if (path_err != 0) {
        return path_err;
    }
    if (!bfree_guest_path_is_under_tmp(oldp, oldname, sizeof(oldname)) ||
        oldname[0] == '\0' ||
        !bfree_guest_path_is_under_tmp(newp, newname, sizeof(newname)) ||
        newname[0] == '\0') {
        return -30; /* EROFS: only /tmp vfiles are writable */
    }
    if (!bfree_guest_tmp_parents_exist(newname)) {
        return -2;
    }
    src = bfree_guest_vfile_find_by_name(oldname);
    if (!src) {
        return -2;
    }
    dst = bfree_guest_vfile_find_by_name(newname);
    if ((flags & BFREE_RENAME_EXCHANGE) != 0) {
        char tmpname[64];

        if (!dst) {
            return -2; /* ENOENT: both paths must exist */
        }
        if (dst == src) {
            return 0;
        }
        if (src->is_dir || dst->is_dir) {
            return -22; /* directory swap would need descendant rewrites */
        }
        n = 0;
        while (src->name[n] != '\0' && n + 1U < sizeof(tmpname)) {
            tmpname[n] = src->name[n];
            ++n;
        }
        tmpname[n] = '\0';
        n = 0;
        while (dst->name[n] != '\0' && n + 1U < sizeof(src->name)) {
            src->name[n] = dst->name[n];
            ++n;
        }
        src->name[n] = '\0';
        n = 0;
        while (tmpname[n] != '\0' && n + 1U < sizeof(dst->name)) {
            dst->name[n] = tmpname[n];
            ++n;
        }
        dst->name[n] = '\0';
        return 0;
    }
    if (dst && dst != src && (flags & BFREE_RENAME_NOREPLACE) != 0) {
        return -17; /* EEXIST */
    }
    if (dst && dst != src) {
        if (dst->is_dir) {
            if (bfree_guest_tmp_has_children(newname)) {
                return -39; /* ENOTEMPTY */
            }
        }
        if (dst->open_refs > 0) {
            dst->name[0] = '\0';
            dst->orphaned = 1;
        } else {
            bfree_guest_vfile_clear_slot(dst);
        }
    }
    if (src->is_dir) {
        rc = bfree_guest_tmp_rename_descendants(oldname, newname);
        if (rc < 0) {
            return rc;
        }
    }
    n = 0;
    while (newname[n] != '\0' && n + 1U < sizeof(src->name)) {
        src->name[n] = newname[n];
        ++n;
    }
    src->name[n] = '\0';
    return 0;
}


static long sys_linux_link(long olddirfd, long oldpath_ptr, long newdirfd, long newpath_ptr)
{
    char oldp[256];
    char newp[256];
    char oname[64];
    char nname[64];
    bfree_guest_vfile_t *src;
    int vidx;
    int rc;
    long path_err;

    if (copy_user_cstr(oldpath_ptr, oldp, sizeof(oldp)) != 0 ||
        copy_user_cstr(newpath_ptr, newp, sizeof(newp)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(olddirfd, oldp, sizeof(oldp));
    if (path_err != 0) {
        return path_err;
    }
    path_err = bfree_guest_path_at(newdirfd, newp, sizeof(newp));
    if (path_err != 0) {
        return path_err;
    }
    if (!bfree_guest_path_is_under_tmp(oldp, oname, sizeof(oname)) ||
        oname[0] == '\0' ||
        !bfree_guest_path_is_under_tmp(newp, nname, sizeof(nname)) ||
        nname[0] == '\0') {
        return -30; /* EROFS */
    }
    src = bfree_guest_vfile_find_by_name(oname);
    if (!src) {
        return -2;
    }
    if (src->is_dir) {
        return -1; /* EPERM */
    }
    if (bfree_guest_vfile_find_by_name(nname)) {
        return -17; /* EEXIST */
    }
    if (!bfree_guest_tmp_parents_exist(nname)) {
        return -2;
    }
    vidx = (int)(src - g_guest_vfiles);
    rc = bfree_guest_alias_add(nname, vidx);
    if (rc < 0) {
        return rc;
    }
    if (src->nlink < 1) {
        src->nlink = 1;
    }
    src->nlink++;
    return 0;
}

static long sys_linux_symlink(long target_ptr, long newdirfd, long linkpath_ptr)
{
    char target[192];
    char linkp[256];
    char vname[64];
    int fd;
    bfree_guest_vfile_t *vf;
    size_t n;
    long path_err;

    if (copy_user_cstr(target_ptr, target, sizeof(target)) != 0 ||
        copy_user_cstr(linkpath_ptr, linkp, sizeof(linkp)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(newdirfd, linkp, sizeof(linkp));
    if (path_err != 0) {
        return path_err;
    }
    if (!bfree_guest_path_is_under_tmp(linkp, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return -30; /* EROFS: only /tmp is writable */
    }
    if (bfree_guest_vfile_find_by_name(vname)) {
        return -17; /* EEXIST */
    }
    fd = bfree_guest_vfile_alloc_slot(vname, 1);
    if (fd < 0) {
        return -28; /* ENOSPC */
    }
    vf = bfree_guest_vfile_from_fd(fd);
    if (!vf) {
        return -5;
    }
    n = 0;
    while (target[n] != '\0' && n + 1U < BFREE_GUEST_VFILE_SIZE) {
        vf->data[n] = (unsigned char)target[n];
        ++n;
    }
    vf->data[n] = '\0';
    vf->len = n;
    vf->is_symlink = 1;
    return 0;
}

// case 30: sys_clock_getres
// 時刻分解能を返す（1μs = 1000ns）
long sys_clock_getres(long clockid, long res_ptr)
{
    struct timespec *res = (struct timespec *)res_ptr;
    (void)clockid;
    if (res == 0) {
        return -1;
    }
    res->tv_sec = 0;
    res->tv_nsec = 1000; // 1μs分解能
    return 0;
}

// case 31: sys_nanosleep
// 指定時間スリープ（timer_manager.hのtimer_set_eventを使用）
long sys_nanosleep(long req_ptr, long rem_ptr)
{
    struct timespec *req = (struct timespec *)req_ptr;
    struct timespec *rem = (struct timespec *)rem_ptr;
    uint64_t sleep_us;

    if (req == 0) {
        return -1;
    }

    sleep_us = ((uint64_t)req->tv_sec * 1000000ULL) + ((uint64_t)req->tv_nsec / 1000ULL);

    if (sleep_us > 0) {
        /*
         * knl_get_current_time() advances from timer IRQ. SYSCALL clears IF (SFMASK),
         * so a pure busy-wait here never sees time advance -> deadlock.
         * Allow IRQs during the wait; SYSRET still restores user RFLAGS from saved R11.
         */
        uint64_t start = knl_get_current_time();
        __asm__ volatile ("sti" ::: "memory");
        while ((knl_get_current_time() - start) < sleep_us) {
            __asm__ volatile ("pause" ::: "memory");
        }
        __asm__ volatile ("cli" ::: "memory");
    }

    if (rem != 0) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

// case 32: sys_clock_nanosleep
long sys_clock_nanosleep(long clockid, long flags, long req_ptr, long rem_ptr)
{
    // flags: 0=相対時刻, TFD_TIMER_ABSTIME=絶対時刻
    (void)clockid;
    (void)flags;
    // 現在は相対時刻のみサポート
    return sys_nanosleep(req_ptr, rem_ptr);
}

// ============================================================
// その他 syscall (33-40) - Wayland動作に必要なシステム情報・端末制御
// ============================================================

// case 33: sys_uname
// システム情報を返す（Wayland/Weston起動時に使用）
long sys_uname(long buf_ptr)
{
    bfree_utsname_t *buf = (bfree_utsname_t *)buf_ptr;
    if (buf == 0) {
        return -1;
    }
    // 各フィールドを初期化
    memset(buf->sysname, 0, sizeof(buf->sysname));
    memset(buf->nodename, 0, sizeof(buf->nodename));
    memset(buf->release, 0, sizeof(buf->release));
    memset(buf->version, 0, sizeof(buf->version));
    memset(buf->machine, 0, sizeof(buf->machine));

    // システム情報を設定
    bfree_copy_cstr(buf->sysname, sizeof(buf->sysname), "B-Free");
    bfree_copy_cstr(buf->nodename, sizeof(buf->nodename), "localhost");
    bfree_copy_cstr(buf->release, sizeof(buf->release), "1.0.0");
    bfree_copy_cstr(buf->version, sizeof(buf->version), "x86_64");
    bfree_copy_cstr(buf->machine, sizeof(buf->machine), "x86_64");

    return 0;
}

// case 34: sys_sysconf
// システム設定を取得（ページサイズ、CPU数など）
long sys_sysconf(long name)
{
    switch ((int)name) {
        case BFREE_SC_PAGESIZE:
            return 4096; // 4KBページ
        case BFREE_SC_NPROCESSORS_CONF:
        case BFREE_SC_NPROCESSORS_ONLN:
            return 1; // シングルCPU（または実際のCPU数）
        case BFREE_SC_CLK_TCK:
            return 100; // 1秒あたりのクロック ticks
        case BFREE_SC_PHYS_PAGES:
            return 1024; // 物理ページ数（例）
        case BFREE_SC_AVPHYS_PAGES:
            return 512; // 利用可能物理ページ数（例）
        case BFREE_SC_OPEN_MAX:
            return 256; // 最大オープンファイル数
        default:
            return -1; // EINVAL
    }
}

// case 35: sys_gethostname
// ホスト名を取得（ディスプレイ名生成に使用）
long sys_gethostname(long name_ptr, long len)
{
    char *name = (char *)name_ptr;
    if (name == 0 || len <= 0) {
        return -1;
    }
    // ホスト名を設定
    bfree_copy_cstr(name, (uint32_t)len, "localhost");
    return 0;
}

// case 36: sys_pause
// シグナルが来るまでスリープ（イベントループで使用）
long sys_pause(void)
{
    // 簡易実装：シグナルが来るまで待つ
    // 現在はダミー（将来的に適切なスリープ機構へ）
    while (!bfree_signal_any_ready()) {
        // ビジーウェイト（将来的に適切なスリープへ）
        __asm__ volatile ("hlt" ::: "memory");
    }
    return -1; // EINTR
}

// case 37: sys_sched_yield
// CPUを譲渡（マルチタスク環境での公平性）
long sys_sched_yield(void)
{
    long sw = bfree_gthr_yield();

    if (sw != 0) {
        return sw;
    }
    __asm__ volatile ("pause" ::: "memory");
    return 0;
}

// case 38: sys_isatty
// ファイルディスクリプタが端末かどうかを判定
long sys_isatty(long fd)
{
    int resolved;

    if (fd >= 0 && fd <= 2) {
        return 1;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved == (int)BFREE_GUEST_DEV_TTY_FD) {
        return 1;
    }
    {
        bfree_guest_ofd_t *ofd = bfree_guest_ofd_from_fd(resolved);
        if (ofd && ofd->target == (int)BFREE_GUEST_DEV_TTY_FD) {
            return 1;
        }
    }
    if (bfree_pty_slot_from_fd(resolved) >= 0) {
        return 1;
    }
    return 0;
}

// case 39: sys_tcgetattr
// 端末属性の取得
long sys_tcgetattr(long fd, long termios_ptr)
{
    bfree_termios_t *termios_p = (bfree_termios_t *)termios_ptr;
    if (termios_p == 0) {
        return -1;
    }

    // fd 0, 1, 2 以外はエラー
    if (fd < 0 || fd > 2) {
        return -1;
    }

    // 端末属性を初期化（標準的な設定）
    return bfree_guest_fill_termios(fd, termios_ptr);
}

// case 40: sys_tcsetattr
// 端末属性の設定
long sys_tcsetattr(long fd, long optional_actions, long termios_ptr)
{
    (void)optional_actions;
    if (termios_ptr == 0) {
        return -1;
    }

    // fd 0, 1, 2 以外はエラー
    if (fd < 0 || fd > 2) {
        return -1;
    }

    return (long)bfree_guest_tty_set_termios(termios_ptr);
}

static void bfree_signal_bit_location(int sig, int *word_index, uint64_t *bit_mask)
{
    int zero_based = sig - 1;
    if (word_index == 0 || bit_mask == 0) {
        return;
    }
    *word_index = zero_based / 64;
    *bit_mask = 1ULL << (zero_based % 64);
}

static int bfree_signal_valid(int sig)
{
    return sig > 0 && sig <= (BFREE_SIGNAL_WORDS * 64);
}

static int bfree_signal_any_ready(void)
{
    for (int index = 0; index < BFREE_SIGNAL_WORDS; ++index) {
        if (g_signal_pending.bits[index] & ~g_signal_mask.bits[index]) {
            return 1;
        }
    }
    return 0;
}

static uint64_t bfree_timespec_to_us(const struct timespec *ts)
{
    if (ts == 0) {
        return 0;
    }
    return ((uint64_t)ts->tv_sec * 1000000ULL) + ((uint64_t)ts->tv_nsec / 1000ULL);
}

static void bfree_us_to_timespec(uint64_t value_us, struct timespec *ts)
{
    if (ts == 0) {
        return;
    }
    ts->tv_sec = (time_t)(value_us / 1000000ULL);
    ts->tv_nsec = (long)((value_us % 1000000ULL) * 1000ULL);
}

static bfree_timerfd_entry_t *bfree_find_timerfd(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);
    int index = resolved - BFREE_TIMERFD_FD_BASE;
    if (index < 0 || index >= BFREE_MAX_TIMERFD) {
        return 0;
    }
    if (!g_timerfd_entries[index].used) {
        return 0;
    }
    return &g_timerfd_entries[index];
}

static long bfree_linux_read_timerfd(long fd, long buf, long count)
{
    bfree_timerfd_entry_t *tfe = bfree_find_timerfd((int)fd);
    uint64_t *dst;
    uint64_t now;

    if (!tfe) {
        return -9; /* not a timerfd — caller continues */
    }
    if (buf == 0 || count < (long)sizeof(uint64_t) || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    for (;;) {
        now = knl_get_current_time();
        /* Soft-expire if IRQ callback was skipped / late. */
        if (tfe->armed && tfe->next_expire_us > 0 && now >= tfe->next_expire_us) {
            tfe->expirations++;
            if (tfe->interval_us > 0) {
                tfe->next_expire_us = now + tfe->interval_us;
            } else {
                tfe->armed = 0;
                tfe->next_expire_us = 0;
            }
        }
        if (tfe->expirations != 0) {
            break;
        }
        if ((tfe->flags & 04000) != 0) { /* TFD_NONBLOCK */
            return -11;
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
    dst = (uint64_t *)(uintptr_t)buf;
    *dst = tfe->expirations;
    tfe->expirations = 0;
    return (long)sizeof(uint64_t);
}

static void bfree_timerfd_purge_all(void)
{
    int i;

    for (i = 0; i < BFREE_MAX_TIMERFD; ++i) {
        if (g_timerfd_entries[i].event_id >= 0) {
            timer_cancel_event(g_timerfd_entries[i].event_id);
        }
        memset(&g_timerfd_entries[i], 0, sizeof(g_timerfd_entries[i]));
        g_timerfd_entries[i].event_id = -1;
    }
}

static void bfree_timerfd_fire(void *arg)
{
    bfree_timerfd_entry_t *entry = (bfree_timerfd_entry_t *)arg;
    uint64_t now;

    if (entry == 0 || !entry->used) {
        return;
    }
    entry->expirations++;
    entry->event_id = -1;
    now = knl_get_current_time();
    if (entry->interval_us > 0) {
        entry->armed = 1;
        entry->next_expire_us = now + entry->interval_us;
        entry->event_id = timer_set_event(entry->next_expire_us, bfree_timerfd_fire, entry);
    } else {
        entry->armed = 0;
        entry->next_expire_us = 0;
    }
}

static int bfree_timerfd_schedule(bfree_timerfd_entry_t *entry, int flags, const struct itimerspec *new_value, struct itimerspec *old_value)
{
    uint64_t now = knl_get_current_time();
    uint64_t value_us;
    uint64_t interval_us;

    if (entry == 0 || new_value == 0) {
        return -1;
    }

    if (old_value) {
        memset(old_value, 0, sizeof(*old_value));
        old_value->it_interval.tv_sec = (time_t)(entry->interval_us / 1000000ULL);
        old_value->it_interval.tv_nsec = (long)((entry->interval_us % 1000000ULL) * 1000ULL);
        if (entry->armed && entry->next_expire_us > now) {
            bfree_us_to_timespec(entry->next_expire_us - now, &old_value->it_value);
        }
    }

    value_us = bfree_timespec_to_us(&new_value->it_value);
    interval_us = bfree_timespec_to_us(&new_value->it_interval);

    if (entry->event_id >= 0) {
        timer_cancel_event(entry->event_id);
        entry->event_id = -1;
    }

    entry->interval_us = interval_us;
    entry->expirations = 0;

    if (value_us == 0) {
        entry->armed = 0;
        entry->next_expire_us = 0;
        return 0;
    }

    if (flags & TFD_TIMER_ABSTIME) {
        entry->next_expire_us = value_us;
    } else {
        entry->next_expire_us = now + value_us;
    }
    entry->armed = 1;
    entry->event_id = timer_set_event(entry->next_expire_us, bfree_timerfd_fire, entry);
    /* Soft-expire path works even if the IRQ event table is full. */
    return 0;
}

typedef long (*syscall_func_t)(long, long, long, long, long, long);

// 個別API雛形（PoC流用可）
static inline uint8_t _inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int bfree_stdin_byte_ready(void)
{
    if (_inb(0x3F8 + 5) & 0x01) {
        return 1;
    }
    /* ROLE_APP (desktop/QPA) owns PS/2 via sys_poll_input_event — do not
     * advertise keyboard bytes as stdin or they are stolen before poll. */
    if (bfree_security_get_role() == BFREE_ROLE_APP) {
        return 0;
    }
    mouse_poll_ps2();
    return keyboard_has_data();
}

static int bfree_stdin_pop_byte(uint8_t *out_ch)
{
    uint32_t keycode;

    if (out_ch == 0) {
        return 0;
    }
    if (_inb(0x3F8 + 5) & 0x01) {
        *out_ch = _inb(0x3F8);
        if (*out_ch == '\r') {
            *out_ch = '\n';
        }
        return 1;
    }
    if (bfree_security_get_role() == BFREE_ROLE_APP) {
        return 0;
    }
    mouse_poll_ps2();
    if (keyboard_pop_char(&keycode)) {
        *out_ch = (uint8_t)keycode;
        return 1;
    }
    return 0;
}

static long bfree_stdin_read_user(long buf, long count)
{
    uint8_t *dst;
    long got = 0;
    unsigned int lflag;
    int canonical;
    unsigned char vmin;

    if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    bfree_guest_tty_ensure_init();
    lflag = g_guest_tty_termios.c_lflag;
    canonical = (lflag & BFREE_ICANON) != 0U;
    vmin = g_guest_tty_termios.c_cc[BFREE_VMIN];
    if (vmin == 0) {
        vmin = 1;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    while (got < count) {
        uint8_t ch;

        /* H06: bg tty soft TTIN */
        if (bfree_guest_tty_is_background()) {
            long er = bfree_guest_tty_soft_job_sig(BFREE_SIGTTIN);
            if (er < 0) {
                return er;
            }
        }
        while (!bfree_stdin_byte_ready()) {
            if (bfree_guest_tty_is_background()) {
                long er = bfree_guest_tty_soft_job_sig(BFREE_SIGTTIN);
                if (er < 0) {
                    return er;
                }
            }
            __asm__ volatile("sti; hlt" ::: "memory");
        }
        if (!bfree_stdin_pop_byte(&ch)) {
            break;
        }
        /* ISIG: VINTR (typically Ctrl+C) → SIGINT, do not inject into the line. */
        if ((lflag & BFREE_ISIG) != 0U &&
            g_guest_tty_termios.c_cc[BFREE_VINTR] != 0 &&
            ch == g_guest_tty_termios.c_cc[BFREE_VINTR]) {
            /* H05: DFL SIGINT terminates the coop child (WIFSIGNALED), not EINTR. */
            if (g_guest_fork_active &&
                g_guest_sig_disp[2] == BFREE_SIG_DFL) {
                bfree_guest_fork_child_pipe_close_writers();
                return bfree_guest_exit_from_fork_signal(2);
            }
            bfree_guest_sig_raise(2);
            {
                int er = bfree_guest_sig_take_eintr();
                if (er < 0) {
                    return got > 0 ? got : er;
                }
            }
            continue;
        }
        if (ch == '\r') {
            ch = '\n';
        }

        if (!canonical) {
            dst[got++] = ch;
            if (got >= (long)vmin || got >= count) {
                break;
            }
            continue;
        }

        if (ch == 127 || ch == 8) {
            if (got > 0) {
                got--;
                if (lflag & BFREE_ECHO) {
                    uart_puts("\b \b");
                }
            }
            continue;
        }
        if (ch == '\n') {
            if (lflag & BFREE_ECHO) {
                uart_putc('\r');
                uart_putc('\n');
            }
            dst[got++] = ch;
            break;
        }
        if (lflag & BFREE_ECHO) {
            uart_putc((char)ch);
        }
        dst[got++] = ch;
    }
    return got;
}

long sys_poll_input_event(long arg1) {
    bfree_raw_input_event_t *out = (bfree_raw_input_event_t *)arg1;
#if defined(BFREE_WAYLAND_INPUT_STRICT) && BFREE_WAYLAND_INPUT_STRICT
    if (bfree_security_get_role() == BFREE_ROLE_APP) {
        g_input_deny_count++;
        if (!g_input_deny_logged_once) {
            g_input_deny_logged_once = 1;
            bfree_audit_log("input_deny", "direct_client_input", g_input_deny_count);
        }
        /* Treat as "no event" for non-compositor clients to avoid retry storms. */
        return 0;
    }
#endif

    if (out == 0) {
        return -1;
    }

    // COM1 serial input (for QEMU -serial stdio: typing in terminal goes to COM1)
    if (_inb(0x3F8 + 5) & 0x01) {
        uint8_t ch = _inb(0x3F8);
        if (out) {
            out->type = 1;
            out->keycode = (uint32_t)ch;
            out->mouse_x = 0;
            out->mouse_y = 0;
            out->mouse_btn = 0;
        }
        return 1;
    }

    /* Drain PS/2 controller before software kbd queue — mouse_poll_ps2 routes kbd vs aux bytes */
    mouse_poll_ps2();

    if (keyboard_has_data()) {
        uint32_t keycode = 0;

        if (!keyboard_pop_char(&keycode)) {
            return 0;
        }

        out->type = 1;
        out->keycode = keycode;
        out->mouse_x = 0;
        out->mouse_y = 0;
        out->mouse_btn = 0;
        return 1;
    }

    if (mouse_has_data()) {
        int mouse_x = 0;
        int mouse_y = 0;
        int mouse_buttons = 0;

        if (!mouse_pop_state(&mouse_x, &mouse_y, &mouse_buttons)) {
            return 0;
        }

        out->type = 3;
        out->keycode = 0;
        out->mouse_x = mouse_x;
        out->mouse_y = mouse_y;
        out->mouse_btn = (uint32_t)mouse_buttons;
        return 1;
    }

    return 0;
}
long sys_get_framebuffer_info(long arg1) {
    bfree_framebuffer_info_t *out = (bfree_framebuffer_info_t *)arg1;
    tk2gpu_fbinfo_t fbinfo;

    if (out == 0) {
        return -1;
    }

    if (runtime_fbdev_ioctl(TK2GPU_IOCTL_GET_INFO, &fbinfo) != 0) {
        out->addr = 0;
        out->pitch = 0;
        out->width = 0;
        out->height = 0;
        out->bpp = 0;
        out->ready = 0;
        return -1;
    }

    out->addr = (void *)(uintptr_t)BFREE_FB0_USER_MMAP_BASE;
    out->pitch = fbinfo.pitch;
    out->width = fbinfo.width;
    out->height = fbinfo.height;
    out->bpp = (uint8_t)fbinfo.bpp;
    out->ready = 1;
    return 0;
}
long sys_clear_screen(long arg1) {
    (void)arg1;
    return 0;
}
long sys_get_time(long arg1) {
    (void)arg1;
    return (long)knl_get_current_time();
}
long sys_input_event_pending(long arg1) {
    (void)arg1;
    /* Must match poll order: QEMU -serial stdio delivers typed keys to COM1 first. */
    if (_inb(0x3F8 + 5) & 0x01) {
        return 1;
    }
    mouse_poll_ps2();
    if (keyboard_has_data()) {
        return 1;
    }
    return mouse_has_data();
}
long sys_timerfd_create(long clockid, long flags) {
    int pub;
    int cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;

    (void)clockid;
    for (int index = 0; index < BFREE_MAX_TIMERFD; ++index) {
        if (!g_timerfd_entries[index].used) {
            g_timerfd_entries[index].used = 1;
            g_timerfd_entries[index].fd = BFREE_TIMERFD_FD_BASE + index;
            g_timerfd_entries[index].flags = (int)flags;
            g_timerfd_entries[index].armed = 0;
            g_timerfd_entries[index].event_id = -1;
            g_timerfd_entries[index].next_expire_us = 0;
            g_timerfd_entries[index].interval_us = 0;
            g_timerfd_entries[index].expirations = 0;
            pub = bfree_guest_fd_publish(g_timerfd_entries[index].fd);
            if (pub < 0) {
                g_timerfd_entries[index].used = 0;
                return -1;
            }
            if (cloexec && pub >= 0 && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    return -1;
}
long sys_timerfd_settime(long fd, long flags, long new_value_ptr, long old_value_ptr) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);
    if (entry == 0) {
        return -1;
    }
    return bfree_timerfd_schedule(entry, (int)flags, (const struct itimerspec *)new_value_ptr, (struct itimerspec *)old_value_ptr);
}
long sys_timerfd_gettime(long fd, long curr_value_ptr) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);
    struct itimerspec *curr_value = (struct itimerspec *)curr_value_ptr;
    uint64_t now = knl_get_current_time();

#ifdef BFREE_RUNTIME_BUILD
    timer_process_events();
    now = knl_get_current_time();
#endif

    if (entry == 0 || curr_value == 0) {
        return -1;
    }

    memset(curr_value, 0, sizeof(*curr_value));
    bfree_us_to_timespec(entry->interval_us, &curr_value->it_interval);
    if (entry->armed && entry->next_expire_us > now) {
        bfree_us_to_timespec(entry->next_expire_us - now, &curr_value->it_value);
    }
    return 0;
}
long sys_timerfd_read(long fd, long value_ptr) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);
    uint64_t *value = (uint64_t *)value_ptr;

#ifdef BFREE_RUNTIME_BUILD
    timer_process_events();
#endif

    if (entry == 0 || value == 0) {
        return -1;
    }
    if (entry->expirations == 0) {
        return -11; /* EAGAIN when nonblocking; blocking handled in read() */
    }
    *value = entry->expirations;
    entry->expirations = 0;
    return 8;
}
long sys_timerfd_pending(long fd) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);
    uint64_t now;

    if (entry == 0) {
        return -1;
    }
    now = knl_get_current_time();
    if (entry->armed && entry->next_expire_us > 0 && now >= entry->next_expire_us) {
        entry->expirations++;
        if (entry->interval_us > 0) {
            entry->next_expire_us = now + entry->interval_us;
        } else {
            entry->armed = 0;
            entry->next_expire_us = 0;
        }
    }
    return entry->expirations > 0;
}
long sys_timerfd_close(long fd) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);
    if (entry == 0) {
        return -1;
    }
    if (entry->event_id >= 0) {
        timer_cancel_event(entry->event_id);
    }
    memset(entry, 0, sizeof(*entry));
    entry->event_id = -1;
    return 0;
}
long sys_signal_setmask(long mask_ptr) {
    bfree_kernel_sigset_t *mask = (bfree_kernel_sigset_t *)mask_ptr;

    if (mask == 0) {
        memset(&g_signal_mask, 0, sizeof(g_signal_mask));
        return 0;
    }

    memcpy(&g_signal_mask, mask, sizeof(g_signal_mask));
    return 0;
}
long sys_signal_pending(long set_ptr) {
    bfree_kernel_sigset_t *set = (bfree_kernel_sigset_t *)set_ptr;

    if (set == 0) {
        return -1;
    }

    memcpy(set, &g_signal_pending, sizeof(g_signal_pending));
    return 0;
}
long sys_signal_post(long pid, long sig) {
    int word_index;
    uint64_t bit_mask;

    (void)pid;
    if (!bfree_signal_valid((int)sig)) {
        return -1;
    }

    bfree_signal_bit_location((int)sig, &word_index, &bit_mask);
    g_signal_pending.bits[word_index] |= bit_mask;
    return 0;
}
long sys_signal_has_ready(long unused) {
    (void)unused;
    return bfree_signal_any_ready();
}
long sys_signal_consume(long mask_ptr, long signo_ptr) {
    bfree_kernel_sigset_t *mask = (bfree_kernel_sigset_t *)mask_ptr;
    int *signo = (int *)signo_ptr;

    if (mask == 0 || signo == 0) {
        return -1;
    }

    for (int index = 0; index < BFREE_SIGNAL_WORDS; ++index) {
        uint64_t ready = g_signal_pending.bits[index] & mask->bits[index];
        if (ready != 0) {
            for (int bit = 0; bit < 64; ++bit) {
                uint64_t bit_mask = 1ULL << bit;
                if (ready & bit_mask) {
                    g_signal_pending.bits[index] &= ~bit_mask;
                    *signo = index * 64 + bit + 1;
                    return 0;
                }
            }
        }
    }

    return 1;
}

long sys_fbdev_ioctl(long fd, long request, long arg) {
    // /dev/fb0のFDなら runtime bridge に橋渡し
    if (fd == BFREE_FB0_FD) {
        return runtime_fbdev_ioctl((int)request, (void *)arg);
    }
    return -1;
}
long sys_input_ioctl(long fd, long request, long arg) {
    // /dev/input/eventX用ioctl分岐（今は雛形）
    (void)fd; (void)request; (void)arg;
    return 0;
}
long sys_ioctl(long fd, long request, long arg) {
    // 汎用ioctl分岐（今は雛形）
    (void)fd; (void)request; (void)arg;
    return 0;
}

long sys_get_tk2_snapshot(long out_ptr, long out_size)
{
    static uint32_t snapshot_calls = 0;
    bfree_tk2_snapshot_t *out = (bfree_tk2_snapshot_t *)out_ptr;
    uint32_t size = (uint32_t)out_size;

    if (out == 0 || size < (uint32_t)sizeof(bfree_tk2_snapshot_t)) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->abi_version = 1U;
    out->flags = 0U;
    out->tick_us = knl_get_current_time();
    out->task_count = 2U;      /* Stage1 test tasks (A/B) */
    out->semaphore_count = 1U; /* mutex path validated */
    out->eventflag_count = 1U; /* timer event path validated */
    out->mailbox_count = 0U;
    out->device_count = 5U;    /* net0/disk0/serial0/fb0/rtc0(placeholder) */

    bfree_copy_cstr(out->kernel_version, (uint32_t)sizeof(out->kernel_version), "T-Kernel 2.02.00 (x86-64 port)");
    bfree_copy_cstr(out->build_date, (uint32_t)sizeof(out->build_date), "2026-05-02");
    bfree_copy_cstr(out->cpu_state, (uint32_t)sizeof(out->cpu_state), "RUNNING");

    snapshot_calls++;
    if ((snapshot_calls & 0xFF) == 1U) {
        uart_puts("[SYSCALL] #23 tk2_snapshot calls=");
        uart_puthex64((uint64_t)snapshot_calls);
        uart_puts("\n");
    }

    return 0;
}

static int copy_user_cstr(long user_ptr, char *out, size_t cap)
{
    const char *p;
    size_t i;

    if (!out || cap < 2) {
        return -1;
    }
    if (user_ptr == 0) {
        return -1;
    }
    p = (const char *)(uintptr_t)user_ptr;
    for (i = 0; i + 1 < cap; ++i) {
        char c = p[i];
        out[i] = c;
        if (c == '\0') {
            return 0;
        }
    }
    out[cap - 1] = '\0';
    return -1;
}

static int validate_initrd_basename(const char *s)
{
    size_t i;

    if (!s || s[0] == '\0') {
        return -1;
    }
    for (i = 0; s[i]; ++i) {
        char c = s[i];
        if (i >= 63) {
            return -1;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            continue;
        }
        return -1;
    }
    return 0;
}

/* Identity-mapped phys page for a user vaddr in the current task PT (see setup_task_stack). */
/* Stack must stay above cloned kernel identity text in the task PT (syscall runs on user CR3).
 * Top 0x01400000 shares VA band with FB mmap base — max span to floor 0x00200000 is 4608 pages (~18 MiB). */
#define BFREE_USER_STACK_MIN_VADDR   0x00200000ULL
#define BFREE_USER_STACK_TOP_DEFAULT 0x01400000ULL
/* Qt desktop.elf: start with 1024 pages (~4 MiB); grow later if needed.
 * Full 4608 exhausted PMM interplay with 40MB ELF on 1GiB guests. */
#define BFREE_USER_STACK_PAGES_EXEC 1024
#define BFREE_USER_STACK_PAGES_BUSYBOX 512
#define BFREE_USER_STACK_PAGES_DESKTOP 1024

/* Real user stack page: User|RW|Present and not identity VA==PA (clone artifact). */
static int bfree_user_stack_page_user_mapped(page_table_t *pt, uint64_t vaddr)
{
    return vmm_user_page_mapped(pt, vaddr);
}

static int bfree_user_stack_ensure_pages(uint64_t stack_top, int pages)
{
    page_table_t *pt;
    int i;
    int mapped_new = 0;

    if (!knl_current_task || !knl_current_task->page_table_base || pages <= 0) {
        return -1;
    }
    if (stack_top < (uint64_t)pages * PAGE_SIZE + BFREE_USER_STACK_MIN_VADDR) {
        uart_puts("[STACK] exec: stack_top too low for pages=");
        uart_puthex64((uint64_t)pages);
        uart_puts("\n");
        return -1;
    }
    uart_puts("[STACK] exec: growing user stack pages=");
    uart_puthex64((uint64_t)pages);
    uart_puts(" (progress every 64 pages)...\n");
    pt = (page_table_t *)knl_current_task->page_table_base;
    bfree_kernel_phys_io_begin();
    for (i = 1; i <= pages; ++i) {
        uint64_t stack_vaddr = stack_top - (uint64_t)i * PAGE_SIZE;
        void *page;

        if (stack_vaddr < BFREE_USER_STACK_MIN_VADDR) {
            uart_puts("[STACK] exec: hit stack floor vaddr=");
            uart_puthex64(stack_vaddr);
            uart_puts("\n");
            bfree_kernel_phys_io_end();
            return -1;
        }
        if (bfree_user_stack_page_user_mapped(pt, stack_vaddr)) {
            continue;
        }
        /* Drop cloned identity supervisor PTE (0x003) before user RW stack page. */
        (void)vmm_unmap_page(pt, stack_vaddr);
        page = pmm_alloc();
        if (!page) {
            uart_puts("[STACK] exec: pmm_alloc failed page=");
            uart_puthex64((uint64_t)i);
            uart_puts(" mapped_new=");
            uart_puthex64((uint64_t)mapped_new);
            uart_puts("\n");
            bfree_kernel_phys_io_end();
            return -1;
        }
        {
            extern uint64_t g_bfree_shell_text_phys;
            uint64_t pp = (uint64_t)(uintptr_t)page;
            if ((g_bfree_elf_watch_phys != 0 && pp == g_bfree_elf_watch_phys) ||
                (g_bfree_shell_text_phys != 0 && pp == g_bfree_shell_text_phys)) {
                uart_puts("[STACK] FATAL: stack page is ELF text phys=");
                uart_puthex64(pp);
                uart_puts("\n");
                bfree_kernel_phys_io_end();
                return -1;
            }
        }
        if (vmm_map_page(pt, stack_vaddr, (uint64_t)page, 0x007ULL) != 0) {
            uart_puts("[STACK] exec: map failed vaddr=");
            uart_puthex64(stack_vaddr);
            uart_puts("\n");
            bfree_kernel_phys_io_end();
            return -1;
        }
        if (bfree_kernel_clear_phys((uint64_t)(uintptr_t)page, PAGE_SIZE) != 0) {
            uart_puts("[STACK] exec: zero failed phys=");
            uart_puthex64((uint64_t)(uintptr_t)page);
            uart_puts("\n");
            bfree_kernel_phys_io_end();
            return -1;
        }
        mapped_new++;
        if ((mapped_new & 0x3F) == 0) {
            uart_puts("[STACK] exec: progress i=");
            uart_puthex64((uint64_t)i);
            uart_puts(" mapped_new=");
            uart_puthex64((uint64_t)mapped_new);
            uart_puts("\n");
        }
    }
    bfree_kernel_phys_io_end();
    uart_puts("[STACK] exec: ensured pages=");
    uart_puthex64((uint64_t)pages);
    uart_puts(" mapped_new=");
    uart_puthex64((uint64_t)mapped_new);
    uart_puts(" top=");
    uart_puthex64(stack_top);
    uart_puts("\n");
    return 0;
}

static uint8_t *bfree_user_stack_page_kptr(uint64_t vaddr)
{
    page_table_t *pt;
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte;

    if (!knl_current_task || !knl_current_task->page_table_base) {
        return 0;
    }
    if (vaddr >= VMM_USER_VA_BYTES) {
        return 0;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    pd_index = (vaddr >> 21) & 0x1FFULL;
    pt_index = (vaddr >> 12) & 0x1FFULL;
    if (pd_index >= PT_LEVEL_COUNT) {
        return 0;
    }
    pte = pt->pt[pd_index][pt_index];
    if (!(pte & 1ULL)) {
        return 0;
    }
    return (uint8_t *)(uintptr_t)(pte & ~(PAGE_SIZE - 1));
}

static int bfree_user_stack_page_phys(uint64_t vaddr, uint64_t *out_phys)
{
    page_table_t *pt;

    if (!knl_current_task || !knl_current_task->page_table_base || !out_phys) {
        return -1;
    }
    if (vaddr >= VMM_USER_VA_BYTES) {
        return -1;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    /* Use VMM helper — direct pt->pt[] misses pt_ext and stale-PD cases. */
    return vmm_user_virt_to_phys(pt, vaddr, out_phys);
}

/* After a large ELF load, force-rebind the top stack pages so prepare_stack
 * always sees live User|RW PTEs (desktop.elf path). */
static int bfree_user_stack_rebind_top(uint64_t stack_top, int n_pages)
{
    page_table_t *pt;
    int i;

    if (!knl_current_task || !knl_current_task->page_table_base || n_pages <= 0) {
        return -1;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    for (i = 1; i <= n_pages; ++i) {
        uint64_t va = stack_top - (uint64_t)i * PAGE_SIZE;
        void *page;
        uint64_t phys = 0;

        if (va < BFREE_USER_STACK_MIN_VADDR) {
            return -1;
        }
        (void)vmm_unmap_page(pt, va);
        page = pmm_alloc();
        if (!page) {
            uart_puts("[STACK] rebind: pmm_alloc fail i=");
            uart_puthex64((uint64_t)i);
            uart_puts("\n");
            return -1;
        }
        if (vmm_map_page(pt, va, (uint64_t)(uintptr_t)page, 0x007ULL) != 0) {
            uart_puts("[STACK] rebind: map fail va=");
            uart_puthex64(va);
            uart_puts("\n");
            return -1;
        }
        vmm_drop_identity_alias(pt, (uint64_t)(uintptr_t)page);
        bfree_kernel_phys_io_begin();
        (void)bfree_kernel_clear_phys((uint64_t)(uintptr_t)page, PAGE_SIZE);
        bfree_kernel_phys_io_end();
        if (i == 1 && vmm_user_virt_to_phys(pt, va, &phys) != 0) {
            uart_puts("[STACK] rebind: verify fail va=");
            uart_puthex64(va);
            uart_puts("\n");
            return -1;
        }
        if (i == 1) {
            uart_puts("[STACK] rebind top ok va=");
            uart_puthex64(va);
            uart_puts(" phys=");
            uart_puthex64(phys);
            uart_puts("\n");
        }
    }
    return 0;
}

#define BFREE_AT_NULL    0UL
#define BFREE_AT_PHDR    3UL
#define BFREE_AT_PHENT   4UL
#define BFREE_AT_PHNUM   5UL
#define BFREE_AT_PAGESZ  6UL
#define BFREE_AT_ENTRY   9UL
#define BFREE_AT_RANDOM  25UL

static int bfree_user_stack_poke_bytes(uint64_t user_vaddr, const char *bytes, uint64_t len)
{
    uint64_t off = 0;
    page_table_t *saved_pt = 0;
    page_table_t *force_pt = 0;

    if (!bytes || len == 0) {
        return 0;
    }
    /* AS-copy: force the running side's PT for the walk (child COW pages). */
    if (g_guest_fork_active && g_guest_fork_was_as_copy && knl_current_task) {
        force_pt = (g_coop_side == 1) ? bfree_process_child_pt()
                                      : bfree_process_parent_pt();
        if (force_pt) {
            saved_pt = (page_table_t *)knl_current_task->page_table_base;
            if (saved_pt != force_pt) {
                knl_current_task->page_table_base = force_pt;
                __asm__ volatile("mov %0, %%cr3" :: "r"(force_pt) : "memory");
            } else {
                saved_pt = 0;
            }
        }
    }
    while (off < len) {
        uint64_t va = user_vaddr + off;
        uint64_t phys;
        uint64_t chunk;
        uint64_t page_off;
        uint8_t *kptr;

        page_off = va & (PAGE_SIZE - 1ULL);
        chunk = PAGE_SIZE - page_off;
        if (chunk > len - off) {
            chunk = len - off;
        }
        if (bfree_user_stack_page_phys(va, &phys) == 0) {
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_poke_phys(phys + page_off, bytes + off, chunk) != 0) {
                bfree_kernel_phys_io_end();
                if (saved_pt) {
                    knl_current_task->page_table_base = saved_pt;
                    __asm__ volatile("mov %0, %%cr3" :: "r"(saved_pt) : "memory");
                }
                return -1;
            }
            bfree_kernel_phys_io_end();
        } else if ((kptr = bfree_user_stack_page_kptr(va)) != 0) {
            uint64_t i;
            for (i = 0; i < chunk; ++i) {
                kptr[page_off + i] = (uint8_t)bytes[off + i];
            }
        } else {
            if (saved_pt) {
                knl_current_task->page_table_base = saved_pt;
                __asm__ volatile("mov %0, %%cr3" :: "r"(saved_pt) : "memory");
            }
            return -1;
        }
        off += chunk;
    }
    if (saved_pt) {
        knl_current_task->page_table_base = saved_pt;
        __asm__ volatile("mov %0, %%cr3" :: "r"(saved_pt) : "memory");
    }
    return 0;
}

static int bfree_user_stack_peek_bytes(uint64_t user_vaddr, char *bytes, uint64_t len)
{
    uint64_t off = 0;

    if (!bytes || len == 0) {
        return 0;
    }
    if (g_guest_fork_active && g_guest_fork_was_as_copy) {
        bfree_coop_as_switch_to(g_coop_side);
    }
    while (off < len) {
        uint64_t va = user_vaddr + off;
        uint64_t phys;
        uint64_t chunk;
        uint64_t page_off;
        uint8_t *kptr;

        page_off = va & (PAGE_SIZE - 1ULL);
        chunk = PAGE_SIZE - page_off;
        if (chunk > len - off) {
            chunk = len - off;
        }
        if (bfree_user_stack_page_phys(va, &phys) == 0) {
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_peek_phys(phys + page_off, bytes + off, chunk) != 0) {
                bfree_kernel_phys_io_end();
                return -1;
            }
            bfree_kernel_phys_io_end();
        } else if ((kptr = bfree_user_stack_page_kptr(va)) != 0) {
            uint64_t i;
            for (i = 0; i < chunk; ++i) {
                bytes[off + i] = (char)kptr[page_off + i];
            }
        } else {
            return -1;
        }
        off += chunk;
    }
    return 0;
}

static int bfree_guest_vfork_stack_snapshot(uint64_t rsp)
{
    uint64_t base;
    uint64_t off;
    uint64_t phys;
    uint64_t i;

    g_guest_fork_stack_save_valid = 0;
    if (rsp < BFREE_VFORK_STACK_SAVE_BYTES / 2ULL) {
        return -1;
    }
    /* Cover locals above rsp and call frames below (child reuses downward). */
    base = (rsp - (BFREE_VFORK_STACK_SAVE_BYTES / 2ULL)) & ~(PAGE_SIZE - 1ULL);
    if (base < BFREE_USER_STACK_MIN_VADDR) {
        base = BFREE_USER_STACK_MIN_VADDR;
    }
    for (off = 0; off < BFREE_VFORK_STACK_SAVE_BYTES; off += PAGE_SIZE) {
        uint64_t va = base + off;

        if (bfree_user_stack_page_phys(va, &phys) != 0) {
            for (i = 0; i < PAGE_SIZE; ++i) {
                g_guest_fork_stack_save[off + i] = 0;
            }
            continue;
        }
        bfree_kernel_phys_io_begin();
        if (bfree_kernel_peek_phys(phys, &g_guest_fork_stack_save[off], PAGE_SIZE) != 0) {
            bfree_kernel_phys_io_end();
            return -1;
        }
        bfree_kernel_phys_io_end();
    }
    g_guest_fork_stack_save_base = base;
    g_guest_fork_stack_save_valid = 1;
    return 0;
}

static void bfree_guest_vfork_stack_restore(void)
{
    uint64_t off;
    uint64_t phys;

    if (!g_guest_fork_stack_save_valid) {
        return;
    }
    for (off = 0; off < BFREE_VFORK_STACK_SAVE_BYTES; off += PAGE_SIZE) {
        uint64_t va = g_guest_fork_stack_save_base + off;

        if (bfree_user_stack_page_phys(va, &phys) != 0) {
            continue;
        }
        bfree_kernel_phys_io_begin();
        (void)bfree_kernel_poke_phys(phys, &g_guest_fork_stack_save[off], PAGE_SIZE);
        bfree_kernel_phys_io_end();
    }
    g_guest_fork_stack_save_valid = 0;
}

static int bfree_guest_basename_eq(const char *a, const char *b)
{
    const char *base = a;
    unsigned i = 0;
    if (!a || !b) {
        return 0;
    }
    while (*base) {
        if (*base == '/') {
            ++base;
            a = base;
            continue;
        }
        ++base;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static void bfree_user_exec_debug_stack(uint64_t user_rsp)
{
    uint64_t phys;
    uint64_t argc_val = 0;
    uint64_t argv0_ptr = 0;
    char arg0[32];
    int i;

    for (i = 0; i < (int)sizeof(arg0); ++i) {
        arg0[i] = '\0';
    }
    if (bfree_user_stack_page_phys(user_rsp, &phys) != 0) {
        uart_puts("[STACK] exec debug: rsp page unmapped rsp=");
        uart_puthex64(user_rsp);
        uart_puts("\n");
        return;
    }
    bfree_kernel_phys_io_begin();
    (void)bfree_kernel_peek_phys(phys + (user_rsp & (PAGE_SIZE - 1ULL)), &argc_val, 8ULL);
    (void)bfree_kernel_peek_phys(phys + ((user_rsp + 8ULL) & (PAGE_SIZE - 1ULL)), &argv0_ptr, 8ULL);
    if (argv0_ptr != 0 &&
        bfree_user_stack_page_phys(argv0_ptr, &phys) == 0) {
        (void)bfree_kernel_peek_phys(phys + (argv0_ptr & (PAGE_SIZE - 1ULL)), arg0,
                                     sizeof(arg0) - 1ULL);
    }
    bfree_kernel_phys_io_end();
    uart_puts("[STACK] exec debug: rsp=");
    uart_puthex64(user_rsp);
    uart_puts(" argc=");
    uart_puthex64(argc_val);
    uart_puts(" argv0=");
    uart_puthex64(argv0_ptr);
    uart_puts(" \"");
    uart_puts(arg0);
    uart_puts("\"\n");
}

static int bfree_user_exec_prepare_musl_stack_argv(uint64_t stack_top, int argc,
    const char *const *argv, int envc, const char *const *envp,
    const bfree_loaded_elf_info_t *elf, uint64_t *out_rsp);

static int bfree_user_exec_prepare_musl_stack(uint64_t stack_top, const char *path,
                                              const bfree_loaded_elf_info_t *elf,
                                              uint64_t *out_rsp)
{
    static const char *const k_busybox_argv[] = {
        "/busybox.elf", "sh", "-i"
    };
    static const char *const k_busybox_env[] = {
        "USER=root",
        "LOGNAME=root",
        "HOME=/root",
        "HOSTNAME=bfree",
        "PATH=/bin:/usr/bin:.",
        "SHELL=/bin/sh",
        "TERM=linux",
        "PS1=root@bfree:# "
    };
    const char *const *argv = 0;
    const char *const *envp = 0;
    int argc = 0;
    int envc = 0;

    if (!out_rsp || stack_top < PAGE_SIZE) {
        return -1;
    }

    if (path && bfree_guest_basename_eq(path, "busybox.elf")) {
        argv = k_busybox_argv;
        argc = 3;
        envp = k_busybox_env;
        envc = 8;
    } else if (path && bfree_guest_basename_eq(path, "desktop.elf")) {
        static const char *const k_desktop_argv[] = { "/desktop.elf" };
        static const char *const k_desktop_env[] = {
            "QT_QPA_PLATFORM=bfree",
            "HOME=/root",
            "USER=root",
            "LOGNAME=root",
            "PATH=/bin:/usr/bin:."
        };
        argv = k_desktop_argv;
        argc = 1;
        envp = k_desktop_env;
        envc = 5;
    } else {
        static const char *const k_default_argv[] = { "program" };
        argv = k_default_argv;
        argc = 1;
        envc = 0;
    }
    return bfree_user_exec_prepare_musl_stack_argv(stack_top, argc, argv, envc, envp, elf, out_rsp);
}

static int bfree_user_exec_prepare_musl_stack_argv(uint64_t stack_top, int argc,
    const char *const *argv, int envc, const char *const *envp,
    const bfree_loaded_elf_info_t *elf, uint64_t *out_rsp)
{
    uint8_t page_buf[4096];
    uint64_t page_base;
    uint64_t argv_vaddr[16];
    uint64_t env_vaddr[16];
    uint64_t qwords[64];
    uint64_t random_ptr;
    uint64_t sp;
    uint64_t pos;
    int qi = 0;
    int ai;
    int i;

    if (!out_rsp || stack_top < PAGE_SIZE || argc <= 0 || !argv) {
        return -1;
    }
    if (argc > (int)(sizeof(argv_vaddr) / sizeof(argv_vaddr[0])) ||
        envc > (int)(sizeof(env_vaddr) / sizeof(env_vaddr[0]))) {
        return -1;
    }

    page_base = stack_top - PAGE_SIZE;
    for (i = 0; i < 4096; ++i) {
        page_buf[i] = 0;
    }

    pos = PAGE_SIZE;
    pos -= 16ULL;
    random_ptr = page_base + pos;
    for (i = 0; i < 16; ++i) {
        page_buf[pos + (uint64_t)i] = (uint8_t)(0xA5U ^ (uint8_t)(i * 17U + 7U));
    }
    pos &= ~0xFULL;

    for (ai = envc - 1; ai >= 0; --ai) {
        const char *s = envp[ai];
        uint64_t n = 0;
        if (!s) {
            return -1;
        }
        while (s[n] != '\0') {
            ++n;
        }
        if (n + 1ULL > pos) {
            return -1;
        }
        pos -= n + 1ULL;
        for (i = 0; s[i] != '\0'; ++i) {
            page_buf[pos + (uint64_t)i] = (uint8_t)s[i];
        }
        page_buf[pos + n] = '\0';
        env_vaddr[ai] = page_base + pos;
    }

    for (ai = argc - 1; ai >= 0; --ai) {
        const char *s = argv[ai];
        uint64_t n = 0;
        if (!s) {
            return -1;
        }
        while (s[n] != '\0') {
            ++n;
        }
        if (n + 1ULL > pos) {
            return -1;
        }
        pos -= n + 1ULL;
        for (i = 0; s[i] != '\0'; ++i) {
            page_buf[pos + (uint64_t)i] = (uint8_t)s[i];
        }
        page_buf[pos + n] = '\0';
        argv_vaddr[ai] = page_base + pos;
    }
    pos &= ~0xFULL;

    qwords[qi++] = (uint64_t)argc;
    for (ai = 0; ai < argc; ++ai) {
        qwords[qi++] = argv_vaddr[ai];
    }
    qwords[qi++] = 0;
    for (ai = 0; ai < envc; ++ai) {
        qwords[qi++] = env_vaddr[ai];
    }
    qwords[qi++] = 0;
    if (elf && elf->valid) {
        qwords[qi++] = BFREE_AT_PAGESZ;
        qwords[qi++] = PAGE_SIZE;
        qwords[qi++] = BFREE_AT_PHDR;
        qwords[qi++] = elf->phdr_vaddr;
        qwords[qi++] = BFREE_AT_PHENT;
        qwords[qi++] = (uint64_t)elf->phentsize;
        qwords[qi++] = BFREE_AT_PHNUM;
        qwords[qi++] = (uint64_t)elf->phnum;
        qwords[qi++] = BFREE_AT_ENTRY;
        qwords[qi++] = elf->entry;
    }
    qwords[qi++] = BFREE_AT_RANDOM;
    qwords[qi++] = random_ptr;
    qwords[qi++] = BFREE_AT_NULL;
    qwords[qi++] = 0;

    if ((uint64_t)qi * 8ULL > pos) {
        return -1;
    }
    sp = (page_base + pos - (uint64_t)qi * 8ULL) & ~0xFULL;
    if (sp < page_base) {
        return -1;
    }
    for (i = 0; i < qi; ++i) {
        uint64_t q = qwords[i];
        uint64_t j;
        for (j = 0; j < 8ULL; ++j) {
            page_buf[(sp - page_base) + (uint64_t)i * 8ULL + j] = (uint8_t)(q & 0xFFU);
            q >>= 8;
        }
    }

    if (bfree_user_stack_poke_bytes(page_base, (const char *)page_buf, PAGE_SIZE) != 0) {
        return -1;
    }

    *out_rsp = sp;
    g_bfree_sysret_exec_rdi = (uint64_t)argc;
    g_bfree_sysret_exec_rsi = (*out_rsp) + 8ULL;
    g_bfree_sysret_exec_rdx = (*out_rsp) + 8ULL + 8ULL * ((uint64_t)argc + 1ULL);
    bfree_user_exec_debug_stack(*out_rsp);
    return 0;
}

static int bfree_copy_user_strarray(long user_arr, char buf[][256], int max, int *out_count)
{
    int n = 0;

    if (!out_count) {
        return -1;
    }
    *out_count = 0;
    if (user_arr == 0) {
        return 0;
    }
    for (;;) {
        long sptr;

        if (n >= max) {
            return -1;
        }
        if (!bfree_user_ptr_mapped(user_arr + (long)n * (long)sizeof(long))) {
            return -1;
        }
        sptr = *(const long *)(uintptr_t)(user_arr + (long)n * (long)sizeof(long));
        if (sptr == 0) {
            break;
        }
        if (copy_user_cstr(sptr, buf[n], 256) != 0) {
            return -1;
        }
        ++n;
    }
    *out_count = n;
    return 0;
}

/* vfork child: load BusyBox into a private address space so the suspended
 * parent's mappings survive. Standalone execve (no fork): replace the current
 * image in-place (shell `exec`, applet re-entry). */
static long sys_linux_execve(long path_ptr, long argv_ptr, long envp_ptr)
{
    static const char *const k_default_env[] = {
        "USER=root",
        "LOGNAME=root",
        "HOME=/root",
        "HOSTNAME=bfree",
        "PATH=/bin:/usr/bin:.",
        "SHELL=/bin/sh",
        "TERM=linux",
        "PS1=root@bfree:# "
    };
    char path[256];
    char argv_buf[16][256];
    char env_buf[16][256];
    const char *argv_ptrs[16];
    const char *env_ptrs[16];
    bfree_loaded_elf_info_t elf;
    page_table_t *child_pt = 0;
    page_table_t *load_pt = 0;
    page_table_t *parent_pt = 0;
    void *entry = 0;
    uint64_t user_rsp = 0;
    uint64_t stack_top;
    int argc = 0;
    int envc = 0;
    int i;
    int use_private_as = 0;
    int is_child;
    int ld;
    const char *exec_img = "busybox.elf";

    is_child = g_guest_fork_active && bfree_process_child_active();

    if (g_execve_kpath_override[0] != '\0') {
        size_t pi;
        for (pi = 0; pi + 1U < sizeof(path) && g_execve_kpath_override[pi] != '\0'; ++pi) {
            path[pi] = g_execve_kpath_override[pi];
        }
        path[pi] = '\0';
        g_execve_kpath_override[0] = '\0';
    } else if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    if (bfree_copy_user_strarray(argv_ptr, argv_buf, 16, &argc) != 0 || argc <= 0) {
        return -14;
    }
    /* Named Multiboot modules keep their image; everything else re-enters busybox. */
    {
        const char *img = "busybox.elf";
        if (bfree_guest_basename_eq(path, "p8test.elf")) {
            img = "p8test.elf";
        } else if (bfree_guest_basename_eq(path, "hello.elf")) {
            img = "hello.elf";
        } else if (bfree_guest_basename_eq(path, "musl_hello.elf")) {
            img = "musl_hello.elf";
        } else if (bfree_guest_basename_eq(path, "ltp_curated.elf")) {
            img = "ltp_curated.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_curated.elf")) {
            img = "libc_test_curated.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_pthread.elf")) {
            img = "libc_test_pthread.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_functional.elf")) {
            img = "libc_test_functional.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_math.elf")) {
            img = "libc_test_math.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_math2.elf")) {
            img = "libc_test_math2.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_math3.elf")) {
            img = "libc_test_math3.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_math4.elf")) {
            img = "libc_test_math4.elf";
        } else if (bfree_guest_basename_eq(path, "libc_test_regression.elf")) {
            img = "libc_test_regression.elf";
        } else if (bfree_guest_basename_eq(path, "abi_hole_finder.elf")) {
            img = "abi_hole_finder.elf";
        }
        exec_img = img;
    }
    /* musl busybox expects argv[0]=/busybox.elf when re-entering from execve. */
    if (exec_img[0] == 'b' && argc + 1 <= 16) {
        int j;
        for (j = argc; j >= 1; --j) {
            memcpy(argv_buf[j], argv_buf[j - 1], sizeof(argv_buf[j]));
        }
        memcpy(argv_buf[0], g_guest_busybox_exe_path, sizeof(argv_buf[0]));
        argv_buf[0][sizeof(argv_buf[0]) - 1] = '\0';
        ++argc;
    }
    for (i = 0; i < argc; ++i) {
        argv_ptrs[i] = argv_buf[i];
    }
    if (envp_ptr != 0 &&
        bfree_copy_user_strarray(envp_ptr, env_buf, 16, &envc) != 0) {
        return -14;
    }
    if (envc <= 0) {
        envc = 8;
        for (i = 0; i < envc; ++i) {
            env_ptrs[i] = k_default_env[i];
        }
    } else {
        for (i = 0; i < envc; ++i) {
            env_ptrs[i] = env_buf[i];
        }
    }

    if (!knl_current_task || !knl_current_task->page_table_base) {
        return -1;
    }
    /* After vfork the task may already be on child_pt — prefer saved parent. */
    parent_pt = bfree_process_parent_pt();
    if (!parent_pt) {
        parent_pt = (page_table_t *)knl_current_task->page_table_base;
    }

    /* Kernel-only bookkeeping is safe for both paths. */
    g_guest_proc_maps_off = 0;
    g_guest_proc_pid_stat_off = 0;
    g_guest_proc_cmdline_off = 0;
    g_guest_proc_meminfo_off = 0;
    g_guest_proc_uptime_off = 0;
    g_guest_proc_loadavg_off = 0;
    g_guest_proc_cpustat_off = 0;
    g_guest_proc_status_off = 0;
    g_guest_proc_mounts_off = 0;
    g_guest_proc_pid2_stat_off = 0;
    g_guest_proc_pid2_cmdline_off = 0;
    g_guest_etc_passwd_off = 0;
    g_guest_etc_group_off = 0;
    g_guest_etc_profile_off = 0;
    g_guest_etc_motd_off = 0;
    bfree_guest_proc_maps_select_busybox(1);

    if (is_child) {
        /*
         * Private AS for the new image: map ELF into child_pt while CR3 stays
         * on the parent (intact stack). Do NOT call execve_reset_subsystems —
         * that unmaps the parent heap. syscall_entry.S switches CR3 with RSP.
         */
        if (bfree_process_exec_commit_as(&child_pt) != 0 || child_pt == 0) {
            return -12; /* ENOMEM */
        }
        use_private_as = 1;
        load_pt = child_pt;
        ld = load_elf_image(exec_img, &entry, load_pt);
        if (ld != 0 || entry == 0) {
            bfree_process_exit_restore_as();
            return -2;
        }
        bfree_loaded_elf_info_get(&elf);
        knl_current_task->page_table_base = child_pt;
        bfree_guest_heap_reset();
        /* Desktop leaves Qt timers/timerfds and may leave stdin on a pipe/pty.
         * Busybox then blocks on the first poll/read without a PF. Do not call
         * full exec_reset (that clears fork state / parent heap).
         * Heal stdin only — keep stdout/stderr redirects (pipe or /tmp) so
         * Terminal capture and ash redirs survive private-AS exec. */
        timer_purge_all();
        bfree_timerfd_purge_all();
        g_guest_clear_child_tid = 0;
        {
            int r0 = bfree_guest_fd_resolve(0);
            if (r0 != 0 && r0 != (int)BFREE_GUEST_DEV_NULL_FD &&
                (bfree_guest_is_pipe_rd(r0) || bfree_guest_is_pipe_wr(r0) ||
                 bfree_pty_slot_from_fd(r0) >= 0 || bfree_guest_is_eventfd(r0))) {
                if (bfree_guest_pipe_slot_from_magic(r0) >= 0) {
                    bfree_guest_pipe_ref(r0, -1);
                }
                g_guest_fd_target[0] = (int)BFREE_GUEST_DEV_NULL_FD;
                g_guest_fd_dup_save[0] = -1;
            }
        }
    } else {
        /* Standalone: replace current image (no parent to preserve). */
        bfree_guest_heap_reset();
        timer_purge_all();
        bfree_timerfd_purge_all();
        bfree_guest_execve_reset_subsystems(1);
        load_pt = parent_pt;
        ld = load_elf_image(exec_img, &entry, load_pt);
        if (ld != 0 || entry == 0) {
            bfree_loaded_elf_info_get(&elf);
            if (!elf.valid || elf.entry == 0) {
                return -2;
            }
            entry = (void *)(uintptr_t)elf.entry;
        } else {
            bfree_loaded_elf_info_get(&elf);
        }
    }

#define BFREE_VFORK_EXEC_STACK_SLOT_PAGES 32

    stack_top = knl_current_task->user_stack_top;
    if (stack_top == 0) {
        stack_top = BFREE_USER_STACK_TOP_DEFAULT;
    }
    {
        uint64_t exec_stack_top =
            stack_top - (uint64_t)BFREE_VFORK_EXEC_STACK_SLOT_PAGES * PAGE_SIZE;

        if (exec_stack_top < BFREE_USER_STACK_MIN_VADDR + PAGE_SIZE) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
        if (bfree_user_stack_ensure_pages(stack_top,
                BFREE_USER_STACK_PAGES_BUSYBOX + BFREE_VFORK_EXEC_STACK_SLOT_PAGES) != 0) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
        if (g_bfree_elf_watch_phys != 0) {
            uint8_t wchk[4];
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_peek_phys(g_bfree_elf_watch_phys + 0xFA0ULL, wchk, 4ULL) == 0) {
                uart_puts("[ELF] post-stack watch bytes=");
                uart_puthex64((uint64_t)wchk[0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[3]);
                uart_puts("\n");
            }
            bfree_kernel_phys_io_end();
        }
        if (bfree_user_exec_prepare_musl_stack_argv(exec_stack_top, argc, argv_ptrs, envc,
                                                    env_ptrs, &elf, &user_rsp) != 0) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
        if (g_bfree_elf_watch_phys != 0) {
            uint8_t wchk[4];
            bfree_kernel_phys_io_begin();
            if (bfree_kernel_peek_phys(g_bfree_elf_watch_phys + 0xFA0ULL, wchk, 4ULL) == 0) {
                uart_puts("[ELF] post-argv watch bytes=");
                uart_puthex64((uint64_t)wchk[0]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[1]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[2]);
                uart_puts(" ");
                uart_puthex64((uint64_t)wchk[3]);
                uart_puts("\n");
            }
            bfree_kernel_phys_io_end();
        }
    }

    (void)path;
    bfree_enable_user_fpu();
    bfree_user_exec_install_fsbase(user_rsp, bfree_guest_basename_eq(exec_img, "busybox.elf") ? 1 : 0);
    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_exec_transfer_rip = (uint64_t)(uintptr_t)entry;
    g_bfree_sysret_exec_rcx = g_bfree_exec_transfer_rip;
    g_bfree_sysret_exec_r11 = 0x202ULL;
    /* Child: switch CR3 with RSP in assembly. Standalone: already on task CR3. */
    g_bfree_sysret_exec_cr3 = use_private_as ? (uint64_t)(uintptr_t)child_pt : 0;
    if (g_bfree_exec_transfer_rip == 0ULL) {
        uart_puts("[ELF] FATAL: exec_transfer_rip=0\n");
        if (use_private_as) {
            knl_current_task->page_table_base = parent_pt;
            bfree_process_exit_restore_as();
        }
        return -2;
    }
    /* Desktop may have CLONE_THREAD (gthr) live; busybox must not inherit those
     * waiter slots or the first futex/poll steals into a dead Qt context. */
    bfree_guest_thread_init();
    if (g_bfree_elf_watch_phys != 0) {
        uint8_t wchk[4];
        bfree_kernel_phys_io_begin();
        if (bfree_kernel_peek_phys(g_bfree_elf_watch_phys + 0xFA0ULL, wchk, 4ULL) == 0) {
            uart_puts("[ELF] pre-transfer watch bytes=");
            uart_puthex64((uint64_t)wchk[0]);
            uart_puts(" ");
            uart_puthex64((uint64_t)wchk[1]);
            uart_puts(" ");
            uart_puthex64((uint64_t)wchk[2]);
            uart_puts(" ");
            uart_puthex64((uint64_t)wchk[3]);
            uart_puts(" phys=");
            uart_puthex64(g_bfree_elf_watch_phys);
            uart_puts("\n");
        }
        bfree_kernel_phys_io_end();
    }
    /* Log the first few syscalls after busybox transfer (smoke: post_exec_syscall). */
    g_bfree_post_exec_syscalls = 4;
    uart_puts("[ELF] exec transfer busybox gthr cleared entry=");
    uart_puthex64(g_bfree_exec_transfer_rip);
    uart_puts(" rsp=");
    uart_puthex64(g_bfree_sysret_exec_rsp);
    uart_puts(" cr3=");
    uart_puthex64(g_bfree_sysret_exec_cr3);
    uart_puts("\n");
    return BFREE_SYSRET_EXEC_TRANSFER;
}

#define BFREE_LINUX_CLONE_VM     0x00000100
#define BFREE_LINUX_CLONE_VFORK  0x00004000
#define BFREE_LINUX_CLONE_THREAD 0x00010000

static long sys_linux_clone(long flags, long newsp, long ptid, long ctid, long tls)
{
    unsigned long f = (unsigned long)flags;

    /* Serial shared-AS coop threads (H26). No preemptive parallel schedule. */
    if ((f & (unsigned long)BFREE_LINUX_CLONE_THREAD) != 0UL) {
        if ((f & (unsigned long)BFREE_LINUX_CLONE_VM) == 0UL) {
            return -38;
        }
        return bfree_guest_thread_clone(f, newsp, ptid, ctid, tls);
    }
    if ((f & (unsigned long)BFREE_LINUX_CLONE_VFORK) != 0UL) {
        return bfree_guest_fork_enter(0);
    }
    /* A2: process-spawn clone (no THREAD/VFORK) ≈ AS-copy fork. Ignore newsp/tls. */
    (void)newsp;
    (void)ptid;
    (void)ctid;
    (void)tls;
    return bfree_guest_fork_enter(1);
}

static int bfree_user_exec_prepare_stack(uint64_t stack_top, void *entry, uint64_t *out_rsp)
{
    bfree_loaded_elf_info_t elf;
    const char *basename = g_bfree_exec_initrd_kpath;
    const char *slash;

    (void)entry;
    if (!out_rsp) {
        return -1;
    }
    bfree_loaded_elf_info_get(&elf);
    slash = basename;
    while (slash && *slash) {
        const char *next = slash;
        while (*next && *next != '/') {
            ++next;
        }
        if (*next == '/') {
            slash = next + 1;
        } else {
            break;
        }
    }
    if (slash && *slash) {
        basename = slash;
    }
    if (bfree_user_exec_prepare_musl_stack(stack_top, g_bfree_exec_initrd_kpath, &elf, out_rsp) == 0) {
        return 0;
    }
    {
        uint64_t zero_qwords[5];
        uint64_t stack_page_phys;
        uint64_t stack_page_vaddr;
        uint64_t user_rsp;
        int i;

        if (stack_top < PAGE_SIZE) {
            return -1;
        }
        stack_page_vaddr = stack_top - PAGE_SIZE;
        if (bfree_user_stack_page_phys(stack_page_vaddr, &stack_page_phys) != 0) {
            uart_puts("[SYSCALL] exec_initrd: user stack page not mapped\n");
            return -1;
        }
        for (i = 0; i < 5; ++i) {
            zero_qwords[i] = 0;
        }
        bfree_kernel_phys_io_begin();
        if (bfree_kernel_poke_phys(stack_page_phys + PAGE_SIZE - sizeof(zero_qwords),
                                   zero_qwords, sizeof(zero_qwords)) != 0) {
            bfree_kernel_phys_io_end();
            uart_puts("[SYSCALL] exec_initrd: stack poke failed\n");
            return -1;
        }
        bfree_kernel_phys_io_end();
        user_rsp = stack_top - sizeof(zero_qwords);
        user_rsp &= ~0xFULL;
        *out_rsp = user_rsp;
    }
    return 0;
}

/* Guest: load ELF from initrd table (Multiboot2 module names) and switch ring3 program.
 * Used for desktop.elf → future guest Qt; see GUEST_QT_DESKTOP.txt */
long sys_exec_initrd(long user_path_ptr)
{
    void *entry = 0;
    uint64_t user_rsp = 0;
    uint64_t stack_top;
    int ld;

    if (!knl_current_task || !knl_current_task->page_table_base) {
        uart_puts("[SYSCALL] exec_initrd: no current task\n");
        return -1;
    }
    if (copy_user_cstr(user_path_ptr, g_bfree_exec_initrd_kpath,
                       sizeof(g_bfree_exec_initrd_kpath)) != 0) {
        uart_puts("[SYSCALL] exec_initrd: bad user path\n");
        return -1;
    }
    if (validate_initrd_basename(g_bfree_exec_initrd_kpath) != 0) {
        uart_puts("[SYSCALL] exec_initrd: rejected path\n");
        return -1;
    }

    uart_puts("[SYSCALL] exec_initrd: loading ");
    uart_puts(g_bfree_exec_initrd_kpath);
    uart_puts(" ...\n");
    bfree_guest_exec_reset_subsystems(
        bfree_guest_basename_eq(g_bfree_exec_initrd_kpath, "busybox.elf") ? 1 : 0);
    timer_purge_all();
    bfree_timerfd_purge_all();
    /* Replace init User PT_LOAD at 0x400000 with identity (busybox is at 0x500000). */
    bfree_exec_unmap_init_legacy((page_table_t *)knl_current_task->page_table_base);
    ld = load_elf_image(g_bfree_exec_initrd_kpath, &entry, knl_current_task->page_table_base);
    if (ld != 0) {
        uart_puts("[SYSCALL] exec_initrd: load_elf_image failed code=");
        uart_puthex64((uint64_t)(int64_t)ld);
        uart_puts(" file=");
        uart_puts(g_bfree_exec_initrd_kpath);
        uart_puts("\n");
        return -1;
    }
    uart_puts("[SYSCALL] exec_initrd: loaded entry=");
    uart_puthex64((uint64_t)(uintptr_t)entry);
    uart_puts("\n");
    if (entry == 0) {
        uart_puts("[SYSCALL] exec_initrd: missing entry point\n");
        return -1;
    }

    stack_top = knl_current_task->user_stack_top;
    if (stack_top == 0) {
        stack_top = BFREE_USER_STACK_TOP_DEFAULT;
#if defined(BFREE_ENABLE_ASLR) && BFREE_ENABLE_ASLR
        {
            uint64_t jitter_pages = (knl_get_current_time() & 0x7ULL);
            stack_top -= jitter_pages * 4096ULL;
        }
#endif
    }
    if (bfree_user_stack_ensure_pages(stack_top,
        bfree_guest_basename_eq(g_bfree_exec_initrd_kpath, "busybox.elf")
            ? BFREE_USER_STACK_PAGES_BUSYBOX
            : (bfree_guest_basename_eq(g_bfree_exec_initrd_kpath, "desktop.elf")
                   ? BFREE_USER_STACK_PAGES_DESKTOP
                   : BFREE_USER_STACK_PAGES_EXEC)) != 0) {
        uart_puts("[SYSCALL] exec_initrd: stack ensure failed\n");
        return -1;
    }
    if (bfree_user_stack_rebind_top(stack_top, 16) != 0) {
        uart_puts("[SYSCALL] exec_initrd: stack rebind failed\n");
        return -1;
    }
    if (bfree_user_exec_prepare_stack(stack_top, entry, &user_rsp) != 0) {
        uart_puts("[SYSCALL] exec_initrd: prepare stack failed\n");
        /* init.elf text was already unmapped — do not return to ring3 init. */
        uart_puts("[SYSCALL] exec_initrd: FATAL hang (no return to init)\n");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    bfree_security_set_role(BFREE_ROLE_APP);

    bfree_enable_user_fpu();

    bfree_user_exec_install_fsbase(user_rsp,
        bfree_guest_basename_eq(g_bfree_exec_initrd_kpath, "busybox.elf") ? 1 : 0);

    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_exec_transfer_rip = (uint64_t)(uintptr_t)entry;
    g_bfree_sysret_exec_rcx = g_bfree_exec_transfer_rip;
    g_bfree_sysret_exec_r11 = 0x202ULL;
    g_bfree_sysret_exec_cr3 = 0;

    bfree_guest_serial_reset_boot_logs();

    /* Last-chance drop: nothing must fire between purge-at-load and ring3 main. */
    timer_purge_all();
    bfree_timerfd_purge_all();

    uart_puts("[SYSCALL] exec_initrd: transfer to ");
    uart_puts(g_bfree_exec_initrd_kpath);
    uart_puts(" entry=");
    uart_puthex64((uint64_t)(uintptr_t)entry);
    uart_puts(" rsp=");
    uart_puthex64(user_rsp);
    uart_puts("\n");
    g_guest_sys_trace = 0;

    return BFREE_SYSRET_EXEC_TRANSFER;
}

/* Phase 2 legacy: read initrd module into guest buffer (DOS FS guest mount). */
long sys_legacy_initrd_read(long user_name_ptr, long offset, long user_buf_ptr, long size)
{
    char name[64];
    uint8_t *dst;
    long rc;

    if (user_name_ptr == 0 || user_buf_ptr == 0 || size <= 0) {
        return -14;
    }
    if (size > (long)(512 * 1024)) {
        return -22;
    }
    if (copy_user_cstr(user_name_ptr, name, sizeof(name)) != 0) {
        return -14;
    }
    if (!bfree_user_ptr_mapped(user_buf_ptr)) {
        return -14;
    }
    dst = (uint8_t *)(uintptr_t)user_buf_ptr;
    rc = bfree_initrd_read(name, (uint64_t)offset, dst, (uint64_t)size);
    if (rc != 0) {
        return -2;
    }
    return size;
}

/* Must never collide with a real syscall return (lseek past 2G returned
 * 0x7FFFFFFF and was misclassified as unhandled → ENOSYS). */
#define BFREE_LINUX_SYSCALL_UNHANDLED ((long)0x6A11C7007FEEDLL)

static int bfree_user_ptr_mapped(long ptr)
{
    return ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ptr);
}

/* --- Qt QEventDispatcherUNIX: pipe2 + epoll (musl calls these syscalls directly) --- */

typedef struct {
    int used;
    int fd;
    uint32_t events;
    /* EPOLLET latch: set once an edge has been reported, cleared when the fd
     * next polls not-ready. EPOLLONESHOT disables the interest after one
     * delivery until EPOLL_CTL_MOD re-arms it. */
    int et;
    int oneshot;
    int et_fired;
    int disabled;
    epoll_data_t data;
} bfree_guest_epoll_watch_t;

typedef struct {
    int used;
    int fd;
    bfree_guest_epoll_watch_t watches[BFREE_MAX_GUEST_EPOLL_WATCHES];
} bfree_guest_epoll_inst_t;

static bfree_guest_epoll_inst_t g_guest_epoll[BFREE_MAX_GUEST_EPOLL];

static int bfree_guest_is_pipe_rd(int fd)
{
    int resolved;
    int slot;

    resolved = bfree_guest_fd_resolve(fd);
    slot = bfree_guest_pipe_slot_from_magic(resolved);
    if (slot < 0 || !g_guest_pipes[slot].used) {
        return 0;
    }
    return !bfree_guest_pipe_is_wr_magic(resolved);
}

static int bfree_guest_is_pipe_wr(int fd)
{
    int resolved;
    int slot;

    resolved = bfree_guest_fd_resolve(fd);
    slot = bfree_guest_pipe_slot_from_magic(resolved);
    if (slot < 0 || !g_guest_pipes[slot].used) {
        return 0;
    }
    return bfree_guest_pipe_is_wr_magic(resolved);
}

static bfree_guest_epoll_inst_t *bfree_guest_find_epoll(int epfd)
{
    int i;
    int resolved = bfree_guest_fd_resolve(epfd);

    for (i = 0; i < BFREE_MAX_GUEST_EPOLL; ++i) {
        if (g_guest_epoll[i].used &&
            (g_guest_epoll[i].fd == epfd || g_guest_epoll[i].fd == resolved)) {
            return &g_guest_epoll[i];
        }
    }
    return 0;
}

static bfree_guest_epoll_watch_t *bfree_guest_find_epoll_watch(bfree_guest_epoll_inst_t *inst, int fd)
{
    int i;

    if (!inst) {
        return 0;
    }
    for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES; ++i) {
        if (inst->watches[i].used && inst->watches[i].fd == fd) {
            return &inst->watches[i];
        }
    }
    return 0;
}

static int bfree_guest_pipe_readable(int fd)
{
    bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd(fd);

    return ps != 0 && ps->len > 0;
}

/* bfree_guest_pipe_reclaim_dead_slots: defined with coop fd snaps below. */

static long sys_linux_pipe2(long pipefd_ptr, long flags)
{
    int *pipefd = (int *)(uintptr_t)pipefd_ptr;
    int i;
    int rd;
    int wr;
    int cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;

    if (pipefd == 0 || !bfree_user_ptr_mapped(pipefd_ptr)) {
        return -14;
    }
    bfree_guest_pipe_reclaim_dead_slots();
    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (!g_guest_pipes[i].used) {
            g_guest_pipes[i].used = 1;
            g_guest_pipes[i].len = 0;
            g_guest_pipes[i].nonblock =
                ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;
            g_guest_pipes[i].wr_open = 1;
            g_guest_pipes[i].rd_open = 1;
            rd = bfree_guest_pipe_magic_fd(i, 0);
            wr = bfree_guest_pipe_magic_fd(i, 1);
            pipefd[0] = bfree_guest_fd_publish(rd);
            pipefd[1] = bfree_guest_fd_publish(wr);
            if (pipefd[0] < 0 || pipefd[1] < 0) {
                if (pipefd[0] >= 0) {
                    (void)sys_linux_close(pipefd[0]);
                }
                if (pipefd[1] >= 0) {
                    (void)sys_linux_close(pipefd[1]);
                }
                g_guest_pipes[i].used = 0;
                return -24;
            }
            if (cloexec) {
                if (pipefd[0] >= 0 && pipefd[0] < BFREE_GUEST_FD_TABLE_SIZE) {
                    g_guest_fd_cloexec[pipefd[0]] = 1;
                }
                if (pipefd[1] >= 0 && pipefd[1] < BFREE_GUEST_FD_TABLE_SIZE) {
                    g_guest_fd_cloexec[pipefd[1]] = 1;
                }
            }
            return 0;
        }
    }
    return -24;
}

static long sys_linux_socketpair(long domain, long type, long protocol, long sv_ptr)
{
    int *sv = (int *)(uintptr_t)sv_ptr;
    int a = -1;
    int b = -1;
    int slot0 = -1;
    int slot1 = -1;
    int i;
    int rd0, wr0, rd1, wr1;
    int pub0, pub1;
    int cloexec = ((unsigned long)type & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    int nonblock = ((unsigned long)type & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;

    (void)domain;
    (void)protocol;
    if (sv == 0 || !bfree_user_ptr_mapped(sv_ptr)) {
        return -14;
    }
    for (i = 0; i < BFREE_UNIX_SLOTS; ++i) {
        if (!g_unix_socks[i].used) {
            if (a < 0) {
                a = i;
            } else {
                b = i;
                break;
            }
        }
    }
    if (a < 0 || b < 0) {
        return -24;
    }
    bfree_guest_pipe_reclaim_dead_slots();
    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (!g_guest_pipes[i].used) {
            if (slot0 < 0) {
                slot0 = i;
            } else {
                slot1 = i;
                break;
            }
        }
    }
    if (slot0 < 0 || slot1 < 0) {
        return -24;
    }
    g_guest_pipes[slot0].used = 1;
    g_guest_pipes[slot0].len = 0;
    g_guest_pipes[slot0].nonblock = nonblock;
    g_guest_pipes[slot0].wr_open = 1;
    g_guest_pipes[slot0].rd_open = 1;
    g_guest_pipes[slot1].used = 1;
    g_guest_pipes[slot1].len = 0;
    g_guest_pipes[slot1].nonblock = nonblock;
    g_guest_pipes[slot1].wr_open = 1;
    g_guest_pipes[slot1].rd_open = 1;
    rd0 = bfree_guest_pipe_magic_fd(slot0, 0);
    wr0 = bfree_guest_pipe_magic_fd(slot0, 1);
    rd1 = bfree_guest_pipe_magic_fd(slot1, 0);
    wr1 = bfree_guest_pipe_magic_fd(slot1, 1);

    g_unix_socks[a].used = 1;
    g_unix_socks[a].listening = 0;
    g_unix_socks[a].connected = 1;
    /* Both ends are pipe-backed even for SOCK_DGRAM: no mailbox routing. */
    g_unix_socks[a].is_dgram = 0;
    g_unix_socks[a].dg_pending = 0;
    g_unix_socks[a].dg_len = 0;
    g_unix_socks[a].pipe_magic = wr0; /* write → peer reads rd0 */
    g_unix_socks[a].accept_rd = rd1;  /* read ← peer writes wr1 */
    g_unix_socks[a].accept_wr = -1;
    g_unix_socks[a].path[0] = '\0';
    g_unix_socks[a].peer_path[0] = '\0';
    bfree_sock_accept_q_reset(g_unix_socks[a].q_rd, g_unix_socks[a].q_wr,
                              &g_unix_socks[a].q_len,
                              &g_unix_socks[a].listen_backlog);

    g_unix_socks[b].used = 1;
    g_unix_socks[b].listening = 0;
    g_unix_socks[b].connected = 1;
    g_unix_socks[b].is_dgram = 0;
    g_unix_socks[b].dg_pending = 0;
    g_unix_socks[b].dg_len = 0;
    g_unix_socks[b].pipe_magic = wr1;
    g_unix_socks[b].accept_rd = rd0;
    g_unix_socks[b].accept_wr = -1;
    g_unix_socks[b].path[0] = '\0';
    g_unix_socks[b].peer_path[0] = '\0';
    bfree_sock_accept_q_reset(g_unix_socks[b].q_rd, g_unix_socks[b].q_wr,
                              &g_unix_socks[b].q_len,
                              &g_unix_socks[b].listen_backlog);

    pub0 = bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + a);
    pub1 = bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + b);
    if (pub0 < 0 || pub1 < 0) {
        if (pub0 >= 0) {
            (void)sys_linux_close(pub0);
        }
        if (pub1 >= 0) {
            (void)sys_linux_close(pub1);
        }
        g_unix_socks[a].used = 0;
        g_unix_socks[b].used = 0;
        g_guest_pipes[slot0].used = 0;
        g_guest_pipes[slot1].used = 0;
        return -24;
    }
    sv[0] = pub0;
    sv[1] = pub1;
    if (cloexec) {
        if (pub0 >= 0 && pub0 < BFREE_GUEST_FD_TABLE_SIZE) {
            g_guest_fd_cloexec[pub0] = 1;
        }
        if (pub1 >= 0 && pub1 < BFREE_GUEST_FD_TABLE_SIZE) {
            g_guest_fd_cloexec[pub1] = 1;
        }
    }
    return 0;
}

static long sys_linux_eventfd2(long count, long flags)
{
    int magic;
    int pub;
    int cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    int nonblock = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;
    int semaphore = ((unsigned long)flags & 1UL) != 0UL; /* EFD_SEMAPHORE */

    if (g_guest_eventfd_next >= BFREE_MAX_GUEST_EVENTFD) {
        return -24;
    }
    g_guest_eventfd_val[g_guest_eventfd_next] = (count != 0) ? (uint64_t)count : 0ULL;
    g_guest_eventfd_nonblock[g_guest_eventfd_next] = nonblock;
    g_guest_eventfd_sem[g_guest_eventfd_next] = semaphore;
    magic = (int)BFREE_GUEST_EVENTFD_BASE + g_guest_eventfd_next++;
    pub = bfree_guest_fd_publish(magic);
    if (pub < 0) {
        g_guest_eventfd_next--;
        g_guest_eventfd_val[g_guest_eventfd_next] = 0;
        g_guest_eventfd_nonblock[g_guest_eventfd_next] = 0;
        g_guest_eventfd_sem[g_guest_eventfd_next] = 0;
        return -24;
    }
    if (cloexec && pub >= 0 && pub < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_cloexec[pub] = 1;
    }
    return pub;
}

/* S3-02: readiness for pipe-backed AF_INET/AF_UNIX (fd bases 0x3A00/0x3900). */
static uint32_t bfree_guest_sock_ready_mask(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);
    int iidx;
    int uidx;
    int mag;
    int slot;
    uint32_t mask = 0;

    iidx = bfree_inet_from_fd(resolved);
    if (iidx >= 0) {
        if (g_inet_socks[iidx].listening) {
            if (g_inet_socks[iidx].q_len > 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_inet_socks[iidx].is_dgram) {
            if (g_inet_socks[iidx].dg_count > 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            mask |= (uint32_t)EPOLLOUT;
            return mask;
        }
        if (g_inet_socks[iidx].connected && g_inet_socks[iidx].pipe_magic >= 0) {
            mag = g_inet_socks[iidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
            }
        }
        if (g_inet_socks[iidx].connected && g_inet_socks[iidx].accept_rd >= 0) {
            mag = g_inet_socks[iidx].accept_rd;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used && g_guest_pipes[slot].len > 0) {
                mask |= (uint32_t)EPOLLIN;
            }
        }
        return mask;
    }

    uidx = bfree_unix_from_fd(resolved);
    if (uidx >= 0) {
        if (g_unix_socks[uidx].listening) {
            if (g_unix_socks[uidx].q_len > 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_unix_socks[uidx].is_dgram) {
            if (g_unix_socks[uidx].dg_pending) {
                mask |= (uint32_t)EPOLLIN;
            }
            mask |= (uint32_t)EPOLLOUT;
            return mask;
        }
        if (g_unix_socks[uidx].connected && g_unix_socks[uidx].pipe_magic >= 0) {
            mag = g_unix_socks[uidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
            }
        }
        if (g_unix_socks[uidx].connected && g_unix_socks[uidx].accept_rd >= 0) {
            mag = g_unix_socks[uidx].accept_rd;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used && g_guest_pipes[slot].len > 0) {
                mask |= (uint32_t)EPOLLIN;
            }
        }
    }
    return mask;
}

/* Minimal SOL_SOCKET options; unknown opts soft-0 (Qt/BusyBox ignore failures). */
#ifndef BFREE_SOL_SOCKET
#define BFREE_SOL_SOCKET 1
#define BFREE_SO_DEBUG 1
#define BFREE_SO_REUSEADDR 2
#define BFREE_SO_TYPE 3
#define BFREE_SO_ERROR 4
#define BFREE_SO_DONTROUTE 5
#define BFREE_SO_BROADCAST 6
#define BFREE_SO_SNDBUF 7
#define BFREE_SO_RCVBUF 8
#define BFREE_SO_KEEPALIVE 9
#define BFREE_SO_LINGER 13
#define BFREE_SO_REUSEPORT 15
#define BFREE_SO_RCVTIMEO 20
#define BFREE_SO_SNDTIMEO 21
#define BFREE_SO_OOBINLINE 10
#define BFREE_SO_ACCEPTCONN 30
#define BFREE_SOCK_STREAM 1
#define BFREE_SOCK_DGRAM 2
/* IPPROTO_IP level options */
#define BFREE_IPPROTO_IP 0
#define BFREE_IP_TOS 1
#define BFREE_IP_TTL 2
#define BFREE_IPPROTO_TCP 6
#define BFREE_TCP_NODELAY 1
#endif
#ifndef BFREE_MSG_DONTWAIT
#define BFREE_MSG_DONTWAIT 0x40
#endif
#ifndef BFREE_MSG_TRUNC
#define BFREE_MSG_TRUNC 0x20
#endif
#ifndef BFREE_MSG_WAITALL
#define BFREE_MSG_WAITALL 0x100
#endif
/* Soft-accepted send/recv flags: out-of-band data is delivered in-band and
 * there are no SCM_RIGHTS ancillary messages to open CLOEXEC. */
#ifndef BFREE_MSG_OOB
#define BFREE_MSG_OOB 0x1
#endif
#ifndef BFREE_MSG_CMSG_CLOEXEC
#define BFREE_MSG_CMSG_CLOEXEC 0x40000000
#endif
#define BFREE_MSG_SOFT_IGNORED \
    ((unsigned long)BFREE_MSG_OOB | (unsigned long)BFREE_MSG_CMSG_CLOEXEC)

typedef struct {
    int l_onoff;
    int l_linger;
} bfree_linger_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} bfree_sock_timeval_t;

static long sys_linux_setsockopt(long fd, long level, long optname, long optval, long optlen)
{
    int resolved;
    int idx;
    int uidx;
    int ival = 0;

    resolved = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd(resolved);
    uidx = bfree_unix_from_fd(resolved);
    if (idx < 0 && uidx < 0) {
        return -88; /* ENOTSOCK */
    }
    if (optlen >= (long)sizeof(int) && optval != 0 && bfree_user_ptr_mapped(optval)) {
        ival = *(const int *)(uintptr_t)optval;
    }
    if (level == BFREE_IPPROTO_IP) {
        if (idx < 0) {
            return 0; /* AF_UNIX: soft-ok */
        }
        if (optname == BFREE_IP_TTL) {
            if (ival < 0 || ival > 255) {
                return -22;
            }
            g_inet_socks[idx].ip_ttl = (ival == 0) ? 64 : ival;
            return 0;
        }
        if (optname == BFREE_IP_TOS) {
            g_inet_socks[idx].ip_tos = ival & 0xFF;
            return 0;
        }
        return 0;
    }
    if (level == BFREE_IPPROTO_TCP) {
        if (idx < 0) {
            return 0;
        }
        if (optname == BFREE_TCP_NODELAY) {
            g_inet_socks[idx].tcp_nodelay = ival ? 1 : 0;
            return 0;
        }
        return 0;
    }
    if (level != BFREE_SOL_SOCKET) {
        return 0; /* soft-ok for other levels */
    }
    if (optname == BFREE_SO_OOBINLINE) {
        if (idx >= 0) {
            g_inet_socks[idx].so_oobinline = ival ? 1 : 0;
        }
        return 0;
    }
    if (optname == BFREE_SO_ERROR) {
        return -92; /* ENOPROTOOPT: SO_ERROR is read-only */
    }
    if (optname == BFREE_SO_REUSEADDR) {
        if (idx >= 0) {
            g_inet_socks[idx].so_reuseaddr = ival ? 1 : 0;
        }
        return 0;
    }
    if (optname == BFREE_SO_REUSEPORT) {
        if (idx >= 0) {
            g_inet_socks[idx].so_reuseport = ival ? 1 : 0;
        }
        return 0;
    }
    if (optname == BFREE_SO_KEEPALIVE) {
        if (idx >= 0) {
            g_inet_socks[idx].so_keepalive = ival ? 1 : 0;
        }
        if (uidx >= 0) {
            g_unix_socks[uidx].so_keepalive = ival ? 1 : 0;
        }
        return 0;
    }
    if (optname == BFREE_SO_RCVBUF) {
        if (ival < 256) {
            ival = 256;
        }
        if (ival > 1024 * 1024) {
            ival = 1024 * 1024;
        }
        if (idx >= 0) {
            g_inet_socks[idx].so_rcvbuf = ival;
        }
        if (uidx >= 0) {
            g_unix_socks[uidx].so_rcvbuf = ival;
        }
        return 0;
    }
    if (optname == BFREE_SO_SNDBUF) {
        if (ival < 256) {
            ival = 256;
        }
        if (ival > 1024 * 1024) {
            ival = 1024 * 1024;
        }
        if (idx >= 0) {
            g_inet_socks[idx].so_sndbuf = ival;
        }
        if (uidx >= 0) {
            g_unix_socks[uidx].so_sndbuf = ival;
        }
        return 0;
    }
    if (optname == BFREE_SO_BROADCAST) {
        if (idx >= 0) {
            g_inet_socks[idx].so_broadcast = ival ? 1 : 0;
        }
        return 0;
    }
    if (optname == BFREE_SO_LINGER) {
        if (optlen >= (long)sizeof(bfree_linger_t) && optval != 0 &&
            bfree_user_ptr_mapped(optval)) {
            const bfree_linger_t *lg = (const bfree_linger_t *)(uintptr_t)optval;
            if (idx >= 0) {
                g_inet_socks[idx].so_linger_on = lg->l_onoff ? 1 : 0;
                g_inet_socks[idx].so_linger_sec = lg->l_linger;
            }
        }
        return 0;
    }
    if (optname == BFREE_SO_RCVTIMEO || optname == BFREE_SO_SNDTIMEO) {
        if (optlen >= (long)sizeof(bfree_sock_timeval_t) && optval != 0 &&
            bfree_user_ptr_mapped(optval)) {
            const bfree_sock_timeval_t *tv =
                (const bfree_sock_timeval_t *)(uintptr_t)optval;
            int64_t us;
            if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
                return -22;
            }
            us = tv->tv_sec * 1000000LL + tv->tv_usec;
            if (idx >= 0) {
                if (optname == BFREE_SO_RCVTIMEO) {
                    g_inet_socks[idx].so_rcvtimeo_us = us;
                } else {
                    g_inet_socks[idx].so_sndtimeo_us = us;
                }
            }
        }
        return 0;
    }
    return 0;
}

static long sys_linux_getsockopt(long fd, long level, long optname, long optval, long optlen_ptr)
{
    int *alen;
    int want;
    int resolved;
    int idx;
    int uidx;
    int value = 0;
    int out_len = (int)sizeof(int);

    if (optval == 0 || optlen_ptr == 0 || !bfree_user_ptr_mapped(optlen_ptr)) {
        return -14;
    }
    alen = (int *)(uintptr_t)optlen_ptr;
    want = *alen;
    if (want < 0) {
        return -22;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd(resolved);
    uidx = bfree_unix_from_fd(resolved);
    if (idx < 0 && uidx < 0) {
        return -88; /* ENOTSOCK */
    }
    if (level == BFREE_SOL_SOCKET) {
        if (optname == BFREE_SO_TYPE) {
            if (idx >= 0) {
                value = g_inet_socks[idx].is_dgram ? BFREE_SOCK_DGRAM : BFREE_SOCK_STREAM;
                if (g_inet_socks[idx].is_raw) {
                    value = 3; /* SOCK_RAW */
                }
            } else {
                value = g_unix_socks[uidx].is_dgram ? BFREE_SOCK_DGRAM
                                                    : BFREE_SOCK_STREAM;
            }
        } else if (optname == BFREE_SO_ERROR) {
            /* Linux semantics: reading SO_ERROR consumes the pending error. */
            if (idx >= 0) {
                value = g_inet_socks[idx].so_error;
                g_inet_socks[idx].so_error = 0;
            } else {
                value = 0;
            }
        } else if (optname == BFREE_SO_OOBINLINE) {
            value = (idx >= 0) ? g_inet_socks[idx].so_oobinline : 0;
        } else if (optname == BFREE_SO_ACCEPTCONN) {
            if (idx >= 0) {
                value = g_inet_socks[idx].listening ? 1 : 0;
            } else {
                value = g_unix_socks[uidx].listening ? 1 : 0;
            }
        } else if (optname == BFREE_SO_REUSEADDR) {
            value = (idx >= 0) ? g_inet_socks[idx].so_reuseaddr : 0;
        } else if (optname == BFREE_SO_REUSEPORT) {
            value = (idx >= 0) ? g_inet_socks[idx].so_reuseport : 0;
        } else if (optname == BFREE_SO_KEEPALIVE) {
            if (idx >= 0) {
                value = g_inet_socks[idx].so_keepalive;
            } else {
                value = g_unix_socks[uidx].so_keepalive;
            }
        } else if (optname == BFREE_SO_RCVBUF) {
            if (idx >= 0) {
                value = g_inet_socks[idx].so_rcvbuf > 0 ? g_inet_socks[idx].so_rcvbuf : 8192;
            } else {
                value = g_unix_socks[uidx].so_rcvbuf > 0 ? g_unix_socks[uidx].so_rcvbuf : 8192;
            }
        } else if (optname == BFREE_SO_SNDBUF) {
            if (idx >= 0) {
                value = g_inet_socks[idx].so_sndbuf > 0 ? g_inet_socks[idx].so_sndbuf : 8192;
            } else {
                value = g_unix_socks[uidx].so_sndbuf > 0 ? g_unix_socks[uidx].so_sndbuf : 8192;
            }
        } else if (optname == BFREE_SO_BROADCAST) {
            value = (idx >= 0) ? g_inet_socks[idx].so_broadcast : 0;
        } else if (optname == BFREE_SO_LINGER) {
            bfree_linger_t lg;
            int copy;

            lg.l_onoff = (idx >= 0) ? g_inet_socks[idx].so_linger_on : 0;
            lg.l_linger = (idx >= 0) ? g_inet_socks[idx].so_linger_sec : 0;
            copy = (int)sizeof(lg);
            if (want < copy) {
                copy = want;
            }
            if (copy > 0 &&
                !bfree_user_buf_mapped((uint64_t)(uintptr_t)optval, (uint64_t)copy)) {
                return -14;
            }
            if (copy > 0) {
                memcpy((void *)(uintptr_t)optval, &lg, (size_t)copy);
            }
            *alen = copy;
            return 0;
        } else if (optname == BFREE_SO_RCVTIMEO || optname == BFREE_SO_SNDTIMEO) {
            bfree_sock_timeval_t tv;
            int64_t us = 0;
            int copy;

            if (idx >= 0) {
                us = (optname == BFREE_SO_RCVTIMEO)
                         ? g_inet_socks[idx].so_rcvtimeo_us
                         : g_inet_socks[idx].so_sndtimeo_us;
            }
            tv.tv_sec = us / 1000000LL;
            tv.tv_usec = us % 1000000LL;
            copy = (int)sizeof(tv);
            if (want < copy) {
                copy = want;
            }
            if (copy > 0 &&
                !bfree_user_buf_mapped((uint64_t)(uintptr_t)optval, (uint64_t)copy)) {
                return -14;
            }
            if (copy > 0) {
                memcpy((void *)(uintptr_t)optval, &tv, (size_t)copy);
            }
            *alen = copy;
            return 0;
        } else {
            value = 0;
        }
    } else if (level == BFREE_IPPROTO_IP) {
        if (optname == BFREE_IP_TTL) {
            value = (idx >= 0 && g_inet_socks[idx].ip_ttl > 0)
                        ? g_inet_socks[idx].ip_ttl
                        : 64;
        } else if (optname == BFREE_IP_TOS) {
            value = (idx >= 0) ? g_inet_socks[idx].ip_tos : 0;
        } else {
            value = 0;
        }
    } else if (level == BFREE_IPPROTO_TCP) {
        if (optname == BFREE_TCP_NODELAY) {
            value = (idx >= 0) ? g_inet_socks[idx].tcp_nodelay : 0;
        } else {
            value = 0;
        }
    } else {
        value = 0;
    }
    if (want < out_len) {
        out_len = want;
    }
    if (out_len > 0 && !bfree_user_buf_mapped((uint64_t)(uintptr_t)optval, (uint64_t)out_len)) {
        return -14;
    }
    if (out_len > 0) {
        memcpy((void *)(uintptr_t)optval, &value, (size_t)out_len);
    }
    *alen = out_len;
    return 0;
}

static long sys_linux_shutdown(long fd, long how)
{
    int resolved;
    int idx;
    int uidx;
    int mag;
    int slot;

    if (how < 0 || how > 2) {
        return -22;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd(resolved);
    uidx = bfree_unix_from_fd(resolved);
    if (idx < 0 && uidx < 0) {
        return -88;
    }
    if (how == 0 || how == 2) { /* SHUT_RD / RDWR */
        if (idx >= 0) {
            g_inet_socks[idx].shut_rd = 1;
        } else {
            g_unix_socks[uidx].shut_rd = 1;
        }
    }
    if (how == 1 || how == 2) { /* SHUT_WR / RDWR */
        if (idx >= 0) {
            g_inet_socks[idx].shut_wr = 1;
            mag = g_inet_socks[idx].pipe_magic;
            /* Drop write endpoint so pipe reclaim does not revive wr_open. */
            g_inet_socks[idx].pipe_magic = -1;
        } else {
            g_unix_socks[uidx].shut_wr = 1;
            mag = g_unix_socks[uidx].pipe_magic;
            g_unix_socks[uidx].pipe_magic = -1;
        }
        /* Closing our write end makes the peer's pipe read see EOF. */
        if (mag >= 0 && bfree_guest_pipe_is_wr_magic(mag)) {
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                g_guest_pipes[slot].wr_open = 0;
            }
        }
    }
    return 0;
}

/* Real bind/connect addr for loopback inet; soft-0 length clamp. */
static long sys_linux_getsockname(long sockfd, long addr, long addrlen_ptr)
{
    int idx;
    int uidx;
    int *alen;
    uint8_t *raw;
    uint16_t port_be;
    uint32_t addr_be;
    uint32_t a;
    int want;
    size_t n;
    size_t path_len;

    if (addr == 0 || addrlen_ptr == 0 || !bfree_user_ptr_mapped(addrlen_ptr)) {
        return -14;
    }
    alen = (int *)(uintptr_t)addrlen_ptr;
    want = *alen;
    if (want < 0) {
        return -22;
    }
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    uidx = bfree_unix_from_fd((int)sockfd);
    if (uidx >= 0) {
        /* Unnamed AF_UNIX (socketpair) or bound path. */
        if (want < 2 || !bfree_user_ptr_mapped(addr)) {
            return -14;
        }
        raw = (uint8_t *)(uintptr_t)addr;
        raw[0] = (uint8_t)(BFREE_LINUX_AF_UNIX & 0xff);
        raw[1] = (uint8_t)((BFREE_LINUX_AF_UNIX >> 8) & 0xff);
        path_len = 0;
        while (path_len < sizeof(g_unix_socks[uidx].path) &&
               g_unix_socks[uidx].path[path_len] != '\0') {
            ++path_len;
        }
        if (path_len == 0) {
            *alen = 2; /* sa_family only */
            return 0;
        }
        n = path_len + 1U; /* include NUL */
        if ((size_t)want < 2U + n) {
            n = (size_t)want > 2U ? (size_t)want - 2U : 0U;
        }
        for (path_len = 0; path_len < n; ++path_len) {
            raw[2 + path_len] = (uint8_t)g_unix_socks[uidx].path[path_len];
        }
        *alen = (int)(2U + n);
        return 0;
    }
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx < 0) {
        return -88; /* ENOTSOCK */
    }
    if (want < 8 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    if (g_inet_socks[idx].is_v6) {
        /* Only loopback is reachable, so the name is always ::1. */
        if (want < 28) {
            return -22;
        }
        port_be = bfree_inet_ntohs(g_inet_socks[idx].port);
        raw = (uint8_t *)(uintptr_t)addr;
        for (n = 0; n < 28U; ++n) {
            raw[n] = 0;
        }
        raw[0] = (uint8_t)(BFREE_LINUX_AF_INET6 & 0xff);
        raw[1] = (uint8_t)((BFREE_LINUX_AF_INET6 >> 8) & 0xff);
        raw[2] = (uint8_t)(port_be & 0xff);
        raw[3] = (uint8_t)((port_be >> 8) & 0xff);
        raw[23] = 1;
        *alen = 28;
        return 0;
    }
    a = g_inet_socks[idx].addr;
    if (!g_inet_socks[idx].bound && !g_inet_socks[idx].connected) {
        a = BFREE_INADDR_ANY;
    } else if (a == BFREE_INADDR_ANY && g_inet_socks[idx].connected) {
        a = BFREE_INADDR_LOOPBACK;
    }
    port_be = bfree_inet_ntohs(g_inet_socks[idx].port); /* host↔be swap */
    addr_be = bfree_inet_ntohl(a);
    raw = (uint8_t *)(uintptr_t)addr;
    raw[0] = (uint8_t)(BFREE_LINUX_AF_INET & 0xff);
    raw[1] = (uint8_t)((BFREE_LINUX_AF_INET >> 8) & 0xff);
    raw[2] = (uint8_t)(port_be & 0xff);
    raw[3] = (uint8_t)((port_be >> 8) & 0xff);
    raw[4] = (uint8_t)(addr_be & 0xff);
    raw[5] = (uint8_t)((addr_be >> 8) & 0xff);
    raw[6] = (uint8_t)((addr_be >> 16) & 0xff);
    raw[7] = (uint8_t)((addr_be >> 24) & 0xff);
    *alen = 16;
    return 0;
}

static long sys_linux_getpeername(long sockfd, long addr, long addrlen_ptr)
{
    int idx;
    int uidx;
    int *alen;
    uint8_t *raw;
    uint16_t port_be;
    uint32_t addr_be;
    int want;
    size_t n;
    size_t path_len;

    if (addr == 0 || addrlen_ptr == 0 || !bfree_user_ptr_mapped(addrlen_ptr)) {
        return -14;
    }
    alen = (int *)(uintptr_t)addrlen_ptr;
    want = *alen;
    if (want < 0) {
        return -22;
    }
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    uidx = bfree_unix_from_fd((int)sockfd);
    if (uidx >= 0) {
        if (!g_unix_socks[uidx].connected) {
            return -107; /* ENOTCONN */
        }
        if (want < 2 || !bfree_user_ptr_mapped(addr)) {
            return -14;
        }
        raw = (uint8_t *)(uintptr_t)addr;
        raw[0] = (uint8_t)(BFREE_LINUX_AF_UNIX & 0xff);
        raw[1] = (uint8_t)((BFREE_LINUX_AF_UNIX >> 8) & 0xff);
        path_len = 0;
        while (path_len < sizeof(g_unix_socks[uidx].path) &&
               g_unix_socks[uidx].path[path_len] != '\0') {
            ++path_len;
        }
        if (path_len == 0) {
            *alen = 2;
            return 0;
        }
        n = path_len + 1U;
        if ((size_t)want < 2U + n) {
            n = (size_t)want > 2U ? (size_t)want - 2U : 0U;
        }
        for (path_len = 0; path_len < n; ++path_len) {
            raw[2 + path_len] = (uint8_t)g_unix_socks[uidx].path[path_len];
        }
        *alen = (int)(2U + n);
        return 0;
    }
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx < 0 || !g_inet_socks[idx].connected) {
        return -107; /* ENOTCONN */
    }
    if (want < 8 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    {
        uint32_t pa = g_inet_socks[idx].peer_addr;
        uint16_t pp = g_inet_socks[idx].peer_port;

        if (pp == 0 && pa == 0) {
            pa = g_inet_socks[idx].addr;
            pp = g_inet_socks[idx].port;
        }
        if (pa == BFREE_INADDR_ANY) {
            pa = BFREE_INADDR_LOOPBACK;
        }
        port_be = bfree_inet_ntohs(pp);
        addr_be = bfree_inet_ntohl(pa);
    }
    raw = (uint8_t *)(uintptr_t)addr;
    raw[0] = (uint8_t)(BFREE_LINUX_AF_INET & 0xff);
    raw[1] = (uint8_t)((BFREE_LINUX_AF_INET >> 8) & 0xff);
    raw[2] = (uint8_t)(port_be & 0xff);
    raw[3] = (uint8_t)((port_be >> 8) & 0xff);
    raw[4] = (uint8_t)(addr_be & 0xff);
    raw[5] = (uint8_t)((addr_be >> 8) & 0xff);
    raw[6] = (uint8_t)((addr_be >> 16) & 0xff);
    raw[7] = (uint8_t)((addr_be >> 24) & 0xff);
    *alen = 16;
    return 0;
}

static long sys_linux_epoll_create1(long flags)
{
    int i;
    int published;
    int cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;

    for (i = 0; i < BFREE_MAX_GUEST_EPOLL; ++i) {
        if (!g_guest_epoll[i].used) {
            int w;

            g_guest_epoll[i].used = 1;
            g_guest_epoll[i].fd = (int)BFREE_GUEST_EPOLL_FD_BASE + i;
            for (w = 0; w < BFREE_MAX_GUEST_EPOLL_WATCHES; ++w) {
                g_guest_epoll[i].watches[w].used = 0;
                g_guest_epoll[i].watches[w].et = 0;
                g_guest_epoll[i].watches[w].oneshot = 0;
                g_guest_epoll[i].watches[w].et_fired = 0;
                g_guest_epoll[i].watches[w].disabled = 0;
            }
            published = bfree_guest_fd_publish(g_guest_epoll[i].fd);
            if (published < 0) {
                g_guest_epoll[i].used = 0;
                return -24;
            }
            if (cloexec && published >= 0 && published < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[published] = 1;
            }
            return published;
        }
    }
    return -24; /* EMFILE */
}

/* Linux x86_64 epoll_event is packed (events@0, data@4). */
typedef struct __attribute__((packed)) {
    uint32_t events;
    uint64_t data;
} bfree_linux_epoll_event_t;

static long sys_linux_epoll_ctl(long epfd, long op, long fd, long event_ptr)
{
    bfree_guest_epoll_inst_t *inst = bfree_guest_find_epoll((int)epfd);
    bfree_linux_epoll_event_t lev;
    bfree_guest_epoll_watch_t *watch;
    int i;
    const uint8_t *src;
    size_t n;

    if (!inst) {
        return -9;
    }
    if (op == EPOLL_CTL_DEL) {
        watch = bfree_guest_find_epoll_watch(inst, (int)fd);
        if (!watch) {
            return -2;
        }
        watch->used = 0;
        return 0;
    }
    if (event_ptr == 0 || !bfree_user_ptr_mapped(event_ptr)) {
        return -14;
    }
    src = (const uint8_t *)(uintptr_t)event_ptr;
    for (n = 0; n < sizeof(lev); ++n) {
        ((uint8_t *)&lev)[n] = src[n];
    }
    watch = bfree_guest_find_epoll_watch(inst, (int)fd);
    if (!watch) {
        for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES; ++i) {
            if (!inst->watches[i].used) {
                watch = &inst->watches[i];
                break;
            }
        }
        if (!watch) {
            return -12; /* ENOMEM */
        }
        watch->used = 1;
        watch->fd = (int)fd;
    }
    watch->events = lev.events;
    watch->et = (lev.events & (uint32_t)EPOLLET) != 0U;
    watch->oneshot = (lev.events & (uint32_t)EPOLLONESHOT) != 0U;
    /* ADD and MOD both re-arm: drop the one-shot disable and the edge latch. */
    watch->et_fired = 0;
    watch->disabled = 0;
    watch->data.u64 = lev.data;
    return 0;
}

/* Raw readiness for an epoll-watched fd, independent of the interest mask. */
static uint32_t bfree_epoll_raw_revents(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);

    if (bfree_guest_is_pipe_rd(fd) && bfree_guest_pipe_readable(fd)) {
        return (uint32_t)EPOLLIN;
    }
    if (bfree_guest_is_eventfd(resolved) &&
        g_guest_eventfd_val[bfree_guest_eventfd_index(resolved)] != 0) {
        return (uint32_t)EPOLLIN;
    }
    if (bfree_find_timerfd(fd) && sys_timerfd_pending(fd) > 0) {
        return (uint32_t)EPOLLIN;
    }
    if (bfree_inotify_has_events(resolved)) {
        return (uint32_t)EPOLLIN;
    }
    /* Valid pidfd for live process: not readable (revents 0). */
    if (bfree_pidfd_is_valid_fd(fd)) {
        return 0;
    }
    return bfree_guest_sock_ready_mask(fd);
}

/* Fill outb with up to maxevents ready interests, honoring EPOLLET/ONESHOT. */
static int bfree_epoll_scan(bfree_guest_epoll_inst_t *inst, uint8_t *outb,
                            int maxevents)
{
    int ready = 0;
    int i;

    for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES && ready < maxevents; ++i) {
        bfree_guest_epoll_watch_t *w = &inst->watches[i];
        uint32_t want;
        uint32_t revents;
        bfree_linux_epoll_event_t lev;
        size_t n;

        if (!w->used || w->disabled) {
            continue;
        }
        want = w->events & ~((uint32_t)EPOLLET | (uint32_t)EPOLLONESHOT);
        revents = bfree_epoll_raw_revents(w->fd) & want;
        if (revents == 0) {
            w->et_fired = 0; /* not ready: the next readiness is a fresh edge */
            continue;
        }
        if (w->et && w->et_fired) {
            continue; /* level still high, but this edge was already reported */
        }
        lev.events = revents;
        lev.data = w->data.u64;
        for (n = 0; n < sizeof(lev); ++n) {
            outb[(size_t)ready * sizeof(lev) + n] = ((const uint8_t *)&lev)[n];
        }
        ++ready;
        if (w->et) {
            w->et_fired = 1;
        }
        if (w->oneshot) {
            w->disabled = 1;
        }
    }
    return ready;
}

static long sys_linux_epoll_wait(long epfd, long events_ptr, long maxevents, long timeout_ms)
{
    bfree_guest_epoll_inst_t *inst = bfree_guest_find_epoll((int)epfd);
    int ready = 0;
    int i;
    uint8_t *outb;

    if (!inst) {
        return -9;
    }
    if (events_ptr == 0 || maxevents <= 0 || !bfree_user_ptr_mapped(events_ptr)) {
        return -14;
    }
    outb = (uint8_t *)(uintptr_t)events_ptr;
    ready = bfree_epoll_scan(inst, outb, (int)maxevents);
    if (ready > 0) {
        return ready;
    }
    if (timeout_ms == 0) {
        /*
         * Non-blocking probe: return 0 when nothing is ready (Linux).
         * (Older Qt busy-spin relied on EINTR; curated LTP and musl expect 0.)
         */
        return 0;
    }
    {
        uint64_t deadline_us = 0;
        int finite = 0;

        if (timeout_ms > 0) {
            finite = 1;
            deadline_us = knl_get_current_time() + (uint64_t)timeout_ms * 1000ULL;
        }
        for (;;) {
            ready = bfree_epoll_scan(inst, outb, (int)maxevents);
            if (ready > 0) {
                return ready;
            }
            if (finite && knl_get_current_time() >= deadline_us) {
                /* Last chance: force soft-expire any armed timerfd watches. */
                for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES; ++i) {
                    bfree_timerfd_entry_t *tfe;
                    if (!inst->watches[i].used) {
                        continue;
                    }
                    tfe = bfree_find_timerfd(inst->watches[i].fd);
                    if (tfe && tfe->armed) {
                        tfe->expirations++;
                        tfe->armed = 0;
                        tfe->next_expire_us = 0;
                    }
                }
                return bfree_epoll_scan(inst, outb, (int)maxevents);
            }
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
}

#ifndef POLLIN
#define POLLIN  0x001
#define POLLOUT 0x004
#endif

static short bfree_guest_poll_revents(int fd, short events)
{
    short revents = 0;
    uint32_t sock_mask;
    int resolved = bfree_guest_fd_resolve(fd);

    if ((events & POLLIN) && bfree_guest_is_pipe_rd(fd) && bfree_guest_pipe_readable(fd)) {
        revents |= POLLIN;
    }
    if ((events & POLLOUT) && bfree_guest_is_pipe_wr(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd(fd);

        if (ps && ps->len < BFREE_GUEST_PIPE_BUF_SIZE) {
            revents |= POLLOUT;
        }
    }
    if ((events & POLLIN) && bfree_guest_is_eventfd(bfree_guest_fd_resolve(fd))) {
        int idx = bfree_guest_eventfd_index(bfree_guest_fd_resolve(fd));

        if (idx >= 0 && g_guest_eventfd_val[idx] != 0) {
            revents |= POLLIN;
        }
    }
    if ((events & POLLIN) && bfree_inotify_has_events(resolved)) {
        revents |= POLLIN;
    }
    if (bfree_pidfd_is_valid_fd(fd)) {
        /* live pidfd: accepted by poll, not ready */
        return revents;
    }
    sock_mask = bfree_guest_sock_ready_mask(fd);
    if ((events & POLLIN) && (sock_mask & (uint32_t)EPOLLIN) != 0U) {
        revents |= POLLIN;
    }
    if ((events & POLLOUT) && (sock_mask & (uint32_t)EPOLLOUT) != 0U) {
        revents |= POLLOUT;
    }
    if ((events & POLLIN) && fd == 0 && bfree_stdin_byte_ready()) {
        revents |= POLLIN;
    }
    return revents;
}

static long sys_linux_poll_common(long fds_ptr, long nfds)
{
    struct pollfd *fds = (struct pollfd *)(uintptr_t)fds_ptr;
    nfds_t n = (nfds_t)nfds;
    nfds_t i;
    long ready = 0;

    if (fds == 0 || nfds <= 0 || !bfree_user_ptr_mapped(fds_ptr)) {
        return -14;
    }
    if ((unsigned long)nfds > 4096UL) {
        return -22;
    }
    for (i = 0; i < n; ++i) {
        short rev;
        long ent = fds_ptr + (long)(i * (int)sizeof(struct pollfd));

        if (!bfree_user_ptr_mapped(ent)
            || !bfree_user_ptr_mapped(ent + (long)sizeof(struct pollfd) - 1)) {
            return -14;
        }
        fds[i].revents = 0;
        rev = bfree_guest_poll_revents(fds[i].fd, fds[i].events);
        if (rev != 0) {
            fds[i].revents = rev;
            ++ready;
        }
    }
    return ready;
}

static long sys_linux_poll(long fds_ptr, long nfds, long timeout_ms)
{
    uint64_t deadline_us = 0;
    int finite = 0;

    if (timeout_ms > 0) {
        finite = 1;
        deadline_us = knl_get_current_time() + (uint64_t)timeout_ms * 1000ULL;
    }
    for (;;) {
        long ready = sys_linux_poll_common(fds_ptr, nfds);

        if (ready > 0) {
            return ready;
        }
        if (timeout_ms == 0) {
            return 0;
        }
        if (finite && knl_get_current_time() >= deadline_us) {
            return 0;
        }
        {
            long sw = bfree_gthr_park_poll(fds_ptr, nfds);

            if (sw != 0) {
                return sw;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static long sys_linux_ppoll(long fds_ptr, long nfds, long timeout_ptr, long sigmask_ptr)
{
    long timeout_ms = -1;
    struct timespec *ts = (struct timespec *)(uintptr_t)timeout_ptr;
    uint64_t deadline_us = 0;
    int finite = 0;
    uint64_t saved_mask = 0;
    int mask_applied = 0;
    long ready;

    if (sigmask_ptr != 0) {
        if (!bfree_user_ptr_mapped(sigmask_ptr)) {
            return -14;
        }
        saved_mask = g_guest_sig_mask;
        g_guest_sig_mask = *(uint64_t *)(uintptr_t)sigmask_ptr;
        g_guest_sig_mask &= ~((1ULL << 8) | (1ULL << 18));
        mask_applied = 1;
    }
    if (timeout_ptr != 0) {
        if (!bfree_user_ptr_mapped(timeout_ptr)) {
            if (mask_applied) {
                g_guest_sig_mask = saved_mask;
            }
            return -14;
        }
        if (ts->tv_sec < 0 || ts->tv_nsec < 0) {
            if (mask_applied) {
                g_guest_sig_mask = saved_mask;
            }
            return -22;
        }
        timeout_ms = (long)(ts->tv_sec * 1000LL + ts->tv_nsec / 1000000LL);
        if (timeout_ms == 0) {
            ready = sys_linux_poll_common(fds_ptr, nfds);
            if (mask_applied) {
                g_guest_sig_mask = saved_mask;
            }
            return ready;
        }
        finite = 1;
        deadline_us = knl_get_current_time()
            + (uint64_t)ts->tv_sec * 1000000ULL
            + (uint64_t)ts->tv_nsec / 1000ULL;
    }
    for (;;) {
        ready = sys_linux_poll_common(fds_ptr, nfds);

        if (ready > 0) {
            break;
        }
        if (timeout_ms == 0) {
            ready = 0;
            break;
        }
        if (finite && knl_get_current_time() >= deadline_us) {
            ready = 0;
            break;
        }
        bfree_guest_alarm_poll();
        {
            long sw = bfree_gthr_park_poll(fds_ptr, nfds);

            if (sw != 0) {
                ready = sw;
                break;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
    if (mask_applied) {
        g_guest_sig_mask = saved_mask;
    }
    return ready;
}

static char g_guest_prctl_name[16] = "bfree";

static long sys_linux_prctl(long option, long arg2, long arg3, long arg4, long arg5)
{
    size_t i;

    (void)arg3;
    (void)arg4;
    (void)arg5;
    if (option == 1) { /* PR_SET_PDEATHSIG */
        if (arg2 < 0 || arg2 > 64) {
            return -22;
        }
        g_guest_pdeathsig = (int)arg2;
        return 0;
    }
    if (option == 2) { /* PR_GET_PDEATHSIG */
        if (arg2 == 0 || !bfree_user_ptr_mapped(arg2)) {
            return -14;
        }
        *(int *)(uintptr_t)arg2 = g_guest_pdeathsig;
        return 0;
    }
    if (option == 15) { /* PR_SET_NAME — copy up to 15 chars + NUL */
        if (arg2 == 0 || !bfree_user_ptr_mapped(arg2)) {
            return -14;
        }
        for (i = 0; i < sizeof(g_guest_prctl_name) - 1U; ++i) {
            char c = ((const char *)(uintptr_t)arg2)[i];
            g_guest_prctl_name[i] = c;
            if (c == '\0') {
                break;
            }
        }
        g_guest_prctl_name[sizeof(g_guest_prctl_name) - 1U] = '\0';
        return 0;
    }
    if (option == 16) { /* PR_GET_NAME — write 16-byte TASK_COMM_LEN buffer */
        if (arg2 == 0 || !bfree_user_ptr_mapped(arg2)) {
            return -14;
        }
        for (i = 0; i < sizeof(g_guest_prctl_name); ++i) {
            ((char *)(uintptr_t)arg2)[i] = g_guest_prctl_name[i];
        }
        return 0;
    }
    return 0;
}

/* Linux guest nanosleep — honor alarm/SIGALRM with EINTR. */
static long sys_linux_nanosleep(long req_ptr, long rem_ptr)
{
    struct timespec *req = (struct timespec *)req_ptr;
    struct timespec *rem = (struct timespec *)rem_ptr;
    uint64_t sleep_us;
    uint64_t start;

    if (req_ptr == 0 || !bfree_user_ptr_mapped(req_ptr)) {
        return -14;
    }
    sleep_us = ((uint64_t)req->tv_sec * 1000000ULL) + ((uint64_t)req->tv_nsec / 1000ULL);
    if (sleep_us == 0) {
        if (rem != 0 && bfree_user_ptr_mapped(rem_ptr)) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        return 0;
    }
    start = knl_get_current_time();
    __asm__ volatile ("sti" ::: "memory");
    while ((knl_get_current_time() - start) < sleep_us) {
        bfree_guest_alarm_poll();
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                __asm__ volatile ("cli" ::: "memory");
                if (rem != 0 && bfree_user_ptr_mapped(rem_ptr)) {
                    uint64_t elapsed = knl_get_current_time() - start;
                    uint64_t left = (elapsed < sleep_us) ? (sleep_us - elapsed) : 0;
                    rem->tv_sec = (long)(left / 1000000ULL);
                    rem->tv_nsec = (long)((left % 1000000ULL) * 1000ULL);
                }
                return er;
            }
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    __asm__ volatile ("cli" ::: "memory");
    if (rem != 0 && bfree_user_ptr_mapped(rem_ptr)) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

static long sys_linux_getrandom(long buf, long buflen, long flags)
{
    uint8_t *p;
    long i;

    /* GRND_NONBLOCK(1)/GRND_RANDOM(2) are both no-ops: the pool never blocks. */
    if (((unsigned long)flags & ~3UL) != 0UL) {
        return -22;
    }
    if (buf == 0 || buflen <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    p = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < buflen; ++i) {
        p[i] = (uint8_t)(0x5A ^ (uint8_t)i);
    }
    return buflen;
}

/* Linux 230: clock_nanosleep, with TIMER_ABSTIME against the uptime clock
 * that backs both CLOCK_REALTIME and CLOCK_MONOTONIC here. */
static long sys_linux_clock_nanosleep(long clockid, long flags, long req_ptr,
                                      long rem_ptr)
{
    const struct timespec *req;
    uint64_t deadline_us;

    (void)clockid;
    (void)rem_ptr;
    if ((flags & 1L) == 0L) { /* relative */
        return sys_linux_nanosleep(req_ptr, rem_ptr);
    }
    if (req_ptr == 0 || !bfree_user_ptr_mapped(req_ptr)) {
        return -14;
    }
    req = (const struct timespec *)(uintptr_t)req_ptr;
    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
        return -22;
    }
    deadline_us = ((uint64_t)req->tv_sec * 1000000ULL) +
                  ((uint64_t)req->tv_nsec / 1000ULL);
    /* TIMER_ABSTIME never reports a remainder, so rem_ptr stays untouched. */
    __asm__ volatile ("sti" ::: "memory");
    while (knl_get_current_time() < deadline_us) {
        bfree_guest_alarm_poll();
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                __asm__ volatile ("cli" ::: "memory");
                return er;
            }
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    __asm__ volatile ("cli" ::: "memory");
    return 0;
}

/* Linux 165/166: record mounts into /proc/mounts (base + dynamic slots). */
static long sys_linux_mount(long source, long target, long fstype, long flags,
                            long data)
{
    char path[96];
    char src[48];
    char fs[24];
    int i;
    int free_slot = -1;

    (void)flags;
    (void)data;
    if (target == 0) {
        return -22;
    }
    if (copy_user_cstr(target, path, sizeof(path)) != 0) {
        return -14;
    }
    if (path[0] == '\0') {
        return -22;
    }
    src[0] = '\0';
    fs[0] = '\0';
    if (source != 0) {
        if (copy_user_cstr(source, src, sizeof(src)) != 0) {
            return -14;
        }
    }
    if (fstype != 0) {
        if (copy_user_cstr(fstype, fs, sizeof(fs)) != 0) {
            return -14;
        }
    }
    for (i = 0; i < BFREE_GUEST_MOUNT_SLOTS; ++i) {
        if (g_guest_dyn_mounts[i].used) {
            if (g_guest_dyn_mounts[i].target[0] != '\0' &&
                strcmp(g_guest_dyn_mounts[i].target, path) == 0) {
                free_slot = i; /* remount / update */
                break;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return -28; /* ENOSPC */
    }
    g_guest_dyn_mounts[free_slot].used = 1;
    bfree_copy_cstr(g_guest_dyn_mounts[free_slot].target,
                    sizeof(g_guest_dyn_mounts[free_slot].target), path);
    bfree_copy_cstr(g_guest_dyn_mounts[free_slot].source,
                    sizeof(g_guest_dyn_mounts[free_slot].source),
                    src[0] ? src : "none");
    bfree_copy_cstr(g_guest_dyn_mounts[free_slot].fstype,
                    sizeof(g_guest_dyn_mounts[free_slot].fstype),
                    fs[0] ? fs : "none");
    bfree_guest_proc_mounts_rebuild();
    return 0;
}

static long sys_linux_umount2(long target, long flags)
{
    char path[96];
    int i;
    int found = 0;

    (void)flags;
    if (target == 0) {
        return -22;
    }
    if (copy_user_cstr(target, path, sizeof(path)) != 0) {
        return -14;
    }
    if (path[0] == '\0') {
        return -22;
    }
    for (i = 0; i < BFREE_GUEST_MOUNT_SLOTS; ++i) {
        if (g_guest_dyn_mounts[i].used &&
            strcmp(g_guest_dyn_mounts[i].target, path) == 0) {
            g_guest_dyn_mounts[i].used = 0;
            g_guest_dyn_mounts[i].target[0] = '\0';
            g_guest_dyn_mounts[i].source[0] = '\0';
            g_guest_dyn_mounts[i].fstype[0] = '\0';
            found = 1;
        }
    }
    if (!found) {
        /* Base mounts stay; soft no-op like prior stub (avoid breaking ash). */
        return 0;
    }
    bfree_guest_proc_mounts_rebuild();
    return 0;
}

/* Linux 434/424: pidfds referencing the init pid, self, or the live coop child. */
#define BFREE_PIDFD_BASE 0x3D00
#define BFREE_MAX_PIDFD  8

static struct {
    int used;
    int pid;
} g_pidfds[BFREE_MAX_PIDFD];

static int bfree_pidfd_slot_from_fd(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);
    int slot = resolved - BFREE_PIDFD_BASE;

    if (slot < 0 || slot >= BFREE_MAX_PIDFD || !g_pidfds[slot].used) {
        return -1;
    }
    return slot;
}

static int bfree_pidfd_is_valid_fd(int fd)
{
    return bfree_pidfd_slot_from_fd(fd) >= 0;
}

static void bfree_pidfd_release(int resolved)
{
    int slot = resolved - BFREE_PIDFD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_PIDFD) {
        g_pidfds[slot].used = 0;
        g_pidfds[slot].pid = 0;
    }
}

static long sys_linux_pidfd_open(long pid, long flags)
{
    int target = (int)pid;
    int self_pid = (int)sys_linux_getpid();
    int i;

    if (target <= 0) {
        return -22;
    }
    if (target != 1 && target != self_pid &&
        !(g_guest_fork_active && target == g_guest_fork_pid)) {
        return -3; /* ESRCH */
    }
    for (i = 0; i < BFREE_MAX_PIDFD; ++i) {
        if (!g_pidfds[i].used) {
            int pub;

            g_pidfds[i].used = 1;
            g_pidfds[i].pid = target;
            pub = bfree_guest_fd_publish(BFREE_PIDFD_BASE + i);
            if (pub < 0) {
                g_pidfds[i].used = 0;
                g_pidfds[i].pid = 0;
                return pub;
            }
            if ((flags & 1L) != 0L && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    return -24; /* EMFILE */
}

static long sys_linux_pidfd_send_signal(long pidfd, long sig, long info,
                                        long flags)
{
    int slot = bfree_pidfd_slot_from_fd((int)pidfd);

    (void)info; /* no siginfo plumbing: the signal is delivered bare */
    if (flags != 0) {
        return -22;
    }
    if (slot < 0) {
        return -9; /* EBADF */
    }
    return sys_linux_kill((long)g_pidfds[slot].pid, sig);
}

/* Linux 438: duplicate a target fd from a pidfd's process into the caller. */
static long sys_linux_pidfd_getfd(long pidfd, long targetfd, long flags)
{
    int slot = bfree_pidfd_slot_from_fd((int)pidfd);
    int self_pid = (int)sys_linux_getpid();
    int target_pid;

    if (flags != 0) {
        return -22;
    }
    if (slot < 0) {
        return -9;
    }
    target_pid = g_pidfds[slot].pid;
    if (target_pid != self_pid &&
        !(g_guest_fork_active && target_pid == g_guest_fork_pid)) {
        return -1; /* EPERM: only self / live coop child */
    }
    return sys_linux_dup(targetfd);
}

/* inotify — watches + small event ring (curated delivery). */
#ifndef BFREE_INOTIFY_FD_BASE
#define BFREE_INOTIFY_FD_BASE      98000
#define BFREE_MAX_INOTIFY          4
#endif
#define BFREE_MAX_INOTIFY_WATCHES  8
#define BFREE_MAX_INOTIFY_EVENTS   8

typedef struct {
    int used;
    int wd;
    uint32_t mask;
    char path[64]; /* absolute watch path, e.g. "/tmp" */
} bfree_inotify_watch_t;

typedef struct {
    int wd;
    uint32_t mask;
    char name[32];
} bfree_inotify_ev_t;

static struct {
    int used;
    int nonblock;
    bfree_inotify_watch_t watches[BFREE_MAX_INOTIFY_WATCHES];
    bfree_inotify_ev_t ev[BFREE_MAX_INOTIFY_EVENTS];
    int ev_head;
    int ev_tail;
    int ev_count;
} g_inotify[BFREE_MAX_INOTIFY];

static int g_inotify_next_wd = 1;

static int bfree_inotify_has_events(int resolved)
{
    int slot = resolved - BFREE_INOTIFY_FD_BASE;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY || !g_inotify[slot].used) {
        return 0;
    }
    return g_inotify[slot].ev_count > 0;
}

static void bfree_inotify_release(int resolved)
{
    int slot = resolved - BFREE_INOTIFY_FD_BASE;
    int i;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY) {
        return;
    }
    g_inotify[slot].used = 0;
    g_inotify[slot].nonblock = 0;
    g_inotify[slot].ev_head = 0;
    g_inotify[slot].ev_tail = 0;
    g_inotify[slot].ev_count = 0;
    for (i = 0; i < BFREE_MAX_INOTIFY_WATCHES; ++i) {
        g_inotify[slot].watches[i].used = 0;
        g_inotify[slot].watches[i].wd = 0;
        g_inotify[slot].watches[i].mask = 0;
        g_inotify[slot].watches[i].path[0] = '\0';
    }
}

static void bfree_inotify_push(int slot, int wd, uint32_t mask, const char *name)
{
    bfree_inotify_ev_t *e;
    size_t i;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY || !g_inotify[slot].used) {
        return;
    }
    if (g_inotify[slot].ev_count >= BFREE_MAX_INOTIFY_EVENTS) {
        return; /* drop */
    }
    e = &g_inotify[slot].ev[g_inotify[slot].ev_tail];
    e->wd = wd;
    e->mask = mask;
    e->name[0] = '\0';
    if (name) {
        for (i = 0; i + 1U < sizeof(e->name) && name[i] != '\0'; ++i) {
            e->name[i] = name[i];
        }
        e->name[i] = '\0';
    }
    g_inotify[slot].ev_tail =
        (g_inotify[slot].ev_tail + 1) % BFREE_MAX_INOTIFY_EVENTS;
    g_inotify[slot].ev_count++;
}

/* vname is the /tmp-relative name (e.g. "ltp_inotify_ev"). */
static void bfree_inotify_notify_vname(const char *vname, uint32_t mask)
{
    char full[80];
    const char *base;
    const char *slash;
    size_t i;
    int s;
    int w;

    if (!vname || vname[0] == '\0') {
        return;
    }
    full[0] = '/';
    full[1] = 't';
    full[2] = 'm';
    full[3] = 'p';
    full[4] = '/';
    for (i = 0; vname[i] != '\0' && i + 6U < sizeof(full); ++i) {
        full[5 + i] = vname[i];
    }
    full[5 + i] = '\0';
    slash = 0;
    for (i = 0; vname[i] != '\0'; ++i) {
        if (vname[i] == '/') {
            slash = vname + i;
        }
    }
    base = slash ? slash + 1 : vname;
    for (s = 0; s < BFREE_MAX_INOTIFY; ++s) {
        if (!g_inotify[s].used) {
            continue;
        }
        for (w = 0; w < BFREE_MAX_INOTIFY_WATCHES; ++w) {
            bfree_inotify_watch_t *wt = &g_inotify[s].watches[w];
            int match = 0;

            if (!wt->used || (wt->mask & mask) == 0U) {
                continue;
            }
            /* Watch /tmp: fire for direct children only. */
            if ((strcmp(wt->path, "/tmp") == 0 ||
                 strcmp(wt->path, "/tmp/") == 0) &&
                slash == 0) {
                match = 1;
            } else if (strcmp(wt->path, full) == 0) {
                match = 1;
            }
            if (match) {
                bfree_inotify_push(s, wt->wd, mask, base);
            }
        }
    }
}

static long bfree_inotify_read(int resolved, long buf, long count)
{
    int slot = resolved - BFREE_INOTIFY_FD_BASE;
    bfree_inotify_ev_t *e;
    uint8_t *dst;
    size_t namelen;
    size_t need;
    size_t i;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY || !g_inotify[slot].used) {
        return -9;
    }
    if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if (g_inotify[slot].ev_count <= 0) {
        return -11; /* EAGAIN (nonblock or soft) */
    }
    e = &g_inotify[slot].ev[g_inotify[slot].ev_head];
    namelen = 0;
    while (e->name[namelen] != '\0') {
        ++namelen;
    }
    namelen += 1U; /* NUL */
    need = 16U + namelen; /* wd+mask+cookie+len + name */
    if ((size_t)count < need) {
        return -22; /* EINVAL */
    }
    dst = (uint8_t *)(uintptr_t)buf;
    /* struct inotify_event layout (x86_64) */
    ((int32_t *)(uintptr_t)dst)[0] = e->wd;
    ((uint32_t *)(uintptr_t)(dst + 4))[0] = e->mask;
    ((uint32_t *)(uintptr_t)(dst + 8))[0] = 0U; /* cookie */
    ((uint32_t *)(uintptr_t)(dst + 12))[0] = (uint32_t)namelen;
    for (i = 0; i < namelen; ++i) {
        dst[16 + i] = (uint8_t)e->name[i];
    }
    g_inotify[slot].ev_head =
        (g_inotify[slot].ev_head + 1) % BFREE_MAX_INOTIFY_EVENTS;
    g_inotify[slot].ev_count--;
    return (long)need;
}

static long sys_linux_inotify_init1(long flags)
{
    int i;

    for (i = 0; i < BFREE_MAX_INOTIFY; ++i) {
        if (!g_inotify[i].used) {
            int pub;

            g_inotify[i].used = 1;
            g_inotify[i].nonblock =
                ((flags & (long)BFREE_LINUX_O_NONBLOCK) != 0L) ? 1 : 0;
            g_inotify[i].ev_head = 0;
            g_inotify[i].ev_tail = 0;
            g_inotify[i].ev_count = 0;
            pub = bfree_guest_fd_publish(BFREE_INOTIFY_FD_BASE + i);
            if (pub < 0) {
                g_inotify[i].used = 0;
                return pub;
            }
            /* IN_CLOEXEC=02000000 */
            if ((flags & 02000000L) != 0L && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    return -24; /* EMFILE */
}

static long sys_linux_inotify_add_watch(long fd, long path_ptr, long mask)
{
    int resolved = bfree_guest_fd_resolve((int)fd);
    int slot = resolved - BFREE_INOTIFY_FD_BASE;
    char path[64];
    int wi;
    int wd;
    size_t i;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY || !g_inotify[slot].used) {
        return -9;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    for (wi = 0; wi < BFREE_MAX_INOTIFY_WATCHES; ++wi) {
        if (!g_inotify[slot].watches[wi].used) {
            break;
        }
    }
    if (wi >= BFREE_MAX_INOTIFY_WATCHES) {
        return -28; /* ENOSPC */
    }
    wd = g_inotify_next_wd++;
    if (wd <= 0) {
        g_inotify_next_wd = 1;
        wd = 1;
    }
    g_inotify[slot].watches[wi].used = 1;
    g_inotify[slot].watches[wi].wd = wd;
    g_inotify[slot].watches[wi].mask = (uint32_t)mask;
    for (i = 0; i + 1U < sizeof(g_inotify[slot].watches[wi].path) &&
                path[i] != '\0';
         ++i) {
        g_inotify[slot].watches[wi].path[i] = path[i];
    }
    g_inotify[slot].watches[wi].path[i] = '\0';
    return (long)wd;
}

static long sys_linux_inotify_rm_watch(long fd, long wd)
{
    int resolved = bfree_guest_fd_resolve((int)fd);
    int slot = resolved - BFREE_INOTIFY_FD_BASE;
    int wi;

    if (slot < 0 || slot >= BFREE_MAX_INOTIFY || !g_inotify[slot].used) {
        return -9;
    }
    for (wi = 0; wi < BFREE_MAX_INOTIFY_WATCHES; ++wi) {
        if (g_inotify[slot].watches[wi].used &&
            g_inotify[slot].watches[wi].wd == (int)wd) {
            g_inotify[slot].watches[wi].used = 0;
            g_inotify[slot].watches[wi].wd = 0;
            g_inotify[slot].watches[wi].mask = 0;
            g_inotify[slot].watches[wi].path[0] = '\0';
            return 0;
        }
    }
    return -22; /* EINVAL */
}

/* ========== Section Q thin ABI holes ========== */

static long sys_linux_getcpu(long cpu_ptr, long node_ptr, long cache_ptr)
{
    (void)cache_ptr;
    if (cpu_ptr != 0) {
        if (!bfree_user_ptr_mapped(cpu_ptr)) {
            return -14;
        }
        *(unsigned *)(uintptr_t)cpu_ptr = 0U;
    }
    if (node_ptr != 0) {
        if (!bfree_user_ptr_mapped(node_ptr)) {
            return -14;
        }
        *(unsigned *)(uintptr_t)node_ptr = 0U;
    }
    return 0;
}

static long sys_linux_ioprio_set(long which, long who, long ioprio)
{
    (void)which;
    (void)who;
    (void)ioprio;
    return 0;
}

static long sys_linux_ioprio_get(long which, long who)
{
    (void)which;
    (void)who;
    return 0; /* IOPRIO_CLASS_NONE */
}

static long sys_linux_chroot(long path_ptr)
{
    char path[256];

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    /* Soft success for curated: do not actually change root. */
    (void)path;
    return 0;
}

static long sys_linux_setns(long fd, long nstype)
{
    (void)fd;
    (void)nstype;
    return 0; /* soft */
}

static long sys_linux_pivot_root(long new_root, long put_old)
{
    (void)new_root;
    (void)put_old;
    return 0; /* soft */
}

/* POSIX timers — thin table of 4, no signal delivery. */
#define BFREE_MAX_PTIMER 4
typedef struct {
    int used;
    int clockid;
    long value_sec;
    long value_nsec;
    long interval_sec;
    long interval_nsec;
} bfree_ptimer_t;
static bfree_ptimer_t g_ptimers[BFREE_MAX_PTIMER];

static long sys_linux_timer_create(long clockid, long sevp, long timerid_ptr)
{
    int i;

    (void)sevp;
    if (timerid_ptr == 0 || !bfree_user_ptr_mapped(timerid_ptr)) {
        return -14;
    }
    for (i = 0; i < BFREE_MAX_PTIMER; ++i) {
        if (!g_ptimers[i].used) {
            g_ptimers[i].used = 1;
            g_ptimers[i].clockid = (int)clockid;
            g_ptimers[i].value_sec = 0;
            g_ptimers[i].value_nsec = 0;
            g_ptimers[i].interval_sec = 0;
            g_ptimers[i].interval_nsec = 0;
            /* musl timer_t is pointer-sized on x86_64 — write a full long. */
            *(long *)(uintptr_t)timerid_ptr = (long)(i + 1);
            return 0;
        }
    }
    return -12; /* ENOMEM */
}

static long sys_linux_timer_settime(long timerid, long flags, long new_ptr,
                                    long old_ptr)
{
    int idx = (int)timerid - 1;
    typedef struct {
        long tv_sec;
        long tv_nsec;
    } bfree_ts_t;
    typedef struct {
        bfree_ts_t it_interval;
        bfree_ts_t it_value;
    } bfree_its_t;
    bfree_its_t *neu;

    (void)flags;
    if (idx < 0 || idx >= BFREE_MAX_PTIMER || !g_ptimers[idx].used) {
        return -22;
    }
    if (old_ptr != 0) {
        bfree_its_t *old;
        if (!bfree_user_ptr_mapped(old_ptr)) {
            return -14;
        }
        old = (bfree_its_t *)(uintptr_t)old_ptr;
        old->it_value.tv_sec = g_ptimers[idx].value_sec;
        old->it_value.tv_nsec = g_ptimers[idx].value_nsec;
        old->it_interval.tv_sec = g_ptimers[idx].interval_sec;
        old->it_interval.tv_nsec = g_ptimers[idx].interval_nsec;
    }
    if (new_ptr == 0 || !bfree_user_ptr_mapped(new_ptr)) {
        return -14;
    }
    neu = (bfree_its_t *)(uintptr_t)new_ptr;
    g_ptimers[idx].value_sec = neu->it_value.tv_sec;
    g_ptimers[idx].value_nsec = neu->it_value.tv_nsec;
    g_ptimers[idx].interval_sec = neu->it_interval.tv_sec;
    g_ptimers[idx].interval_nsec = neu->it_interval.tv_nsec;
    return 0;
}

static long sys_linux_timer_gettime(long timerid, long curr_ptr)
{
    int idx = (int)timerid - 1;
    typedef struct {
        long tv_sec;
        long tv_nsec;
    } bfree_ts_t;
    typedef struct {
        bfree_ts_t it_interval;
        bfree_ts_t it_value;
    } bfree_its_t;
    bfree_its_t *cur;

    if (idx < 0 || idx >= BFREE_MAX_PTIMER || !g_ptimers[idx].used) {
        return -22;
    }
    if (curr_ptr == 0 || !bfree_user_ptr_mapped(curr_ptr)) {
        return -14;
    }
    cur = (bfree_its_t *)(uintptr_t)curr_ptr;
    cur->it_value.tv_sec = g_ptimers[idx].value_sec;
    cur->it_value.tv_nsec = g_ptimers[idx].value_nsec;
    cur->it_interval.tv_sec = g_ptimers[idx].interval_sec;
    cur->it_interval.tv_nsec = g_ptimers[idx].interval_nsec;
    return 0;
}

static long sys_linux_timer_delete(long timerid)
{
    int idx = (int)timerid - 1;

    if (idx < 0 || idx >= BFREE_MAX_PTIMER || !g_ptimers[idx].used) {
        return -22;
    }
    g_ptimers[idx].used = 0;
    return 0;
}

/* SysV shm thin */
#define BFREE_MAX_SYSV_SHM 4
#define BFREE_IPC_PRIVATE  0
#define BFREE_IPC_RMID     0
#define BFREE_IPC_STAT     2
typedef struct {
    int used;
    size_t size;
    long addr;
} bfree_sysv_shm_t;
static bfree_sysv_shm_t g_sysv_shm[BFREE_MAX_SYSV_SHM];

static long sys_linux_shmget(long key, long size, long shmflg)
{
    int i;

    (void)key;
    (void)shmflg;
    if (size <= 0) {
        return -22;
    }
    for (i = 0; i < BFREE_MAX_SYSV_SHM; ++i) {
        if (!g_sysv_shm[i].used) {
            g_sysv_shm[i].used = 1;
            g_sysv_shm[i].size = (size_t)size;
            g_sysv_shm[i].addr = 0;
            return (long)(i + 1);
        }
    }
    return -28; /* ENOSPC */
}

static long sys_linux_shmat(long shmid, long shmaddr, long shmflg)
{
    int idx = (int)shmid - 1;
    long addr;

    (void)shmflg;
    if (idx < 0 || idx >= BFREE_MAX_SYSV_SHM || !g_sysv_shm[idx].used) {
        return -22;
    }
    if (shmaddr != 0) {
        return -22; /* only kernel-chosen VA */
    }
    if (g_sysv_shm[idx].addr != 0) {
        return g_sysv_shm[idx].addr;
    }
    addr = sys_mmap_anonymous_heap(0, (long)g_sysv_shm[idx].size, MAP_ANONYMOUS | MAP_PRIVATE);
    if (addr < 0) {
        return addr;
    }
    g_sysv_shm[idx].addr = addr;
    return addr;
}

static long sys_linux_shmdt(long shmaddr)
{
    int i;

    for (i = 0; i < BFREE_MAX_SYSV_SHM; ++i) {
        if (g_sysv_shm[i].used && g_sysv_shm[i].addr == shmaddr) {
            g_sysv_shm[i].addr = 0;
            return 0;
        }
    }
    return 0; /* soft */
}

static long sys_linux_shmctl(long shmid, long cmd, long buf)
{
    int idx = (int)shmid - 1;

    if (idx < 0 || idx >= BFREE_MAX_SYSV_SHM || !g_sysv_shm[idx].used) {
        return -22;
    }
    if (cmd == BFREE_IPC_RMID) {
        g_sysv_shm[idx].used = 0;
        g_sysv_shm[idx].addr = 0;
        g_sysv_shm[idx].size = 0;
        return 0;
    }
    if (cmd == BFREE_IPC_STAT) {
        if (buf != 0 && bfree_user_ptr_mapped(buf)) {
            uint8_t *p = (uint8_t *)(uintptr_t)buf;
            size_t i;
            for (i = 0; i < 112U; ++i) {
                p[i] = 0;
            }
            /* shm_segsz roughly at offset 48 on x86_64 linux shmid_ds — soft zeros OK */
        }
        return 0;
    }
    return 0;
}

/* SysV sem thin (Linux x86_64: semget 64, semop 65, semctl 66) */
#define BFREE_MAX_SYSV_SEM 4
#define BFREE_SEM_GETVAL   12
#define BFREE_SEM_SETVAL   16
typedef struct {
    int used;
    int nsems;
    int vals[16];
} bfree_sysv_sem_t;
static bfree_sysv_sem_t g_sysv_sem[BFREE_MAX_SYSV_SEM];

static long sys_linux_semget(long key, long nsems, long semflg)
{
    int i;

    (void)key;
    (void)semflg;
    if (nsems <= 0 || nsems > 16) {
        return -22;
    }
    for (i = 0; i < BFREE_MAX_SYSV_SEM; ++i) {
        if (!g_sysv_sem[i].used) {
            int j;
            g_sysv_sem[i].used = 1;
            g_sysv_sem[i].nsems = (int)nsems;
            for (j = 0; j < 16; ++j) {
                g_sysv_sem[i].vals[j] = 0;
            }
            return (long)(i + 1);
        }
    }
    return -28;
}

static long sys_linux_semop(long semid, long sops, long nsops)
{
    int idx = (int)semid - 1;

    (void)sops;
    (void)nsops;
    if (idx < 0 || idx >= BFREE_MAX_SYSV_SEM || !g_sysv_sem[idx].used) {
        return -22;
    }
    return 0; /* soft success */
}

static long sys_linux_semctl(long semid, long semnum, long cmd, long arg)
{
    int idx = (int)semid - 1;

    if (idx < 0 || idx >= BFREE_MAX_SYSV_SEM || !g_sysv_sem[idx].used) {
        return -22;
    }
    if (cmd == BFREE_IPC_RMID) {
        g_sysv_sem[idx].used = 0;
        g_sysv_sem[idx].nsems = 0;
        return 0;
    }
    if (cmd == BFREE_SEM_GETVAL) {
        if (semnum < 0 || semnum >= g_sysv_sem[idx].nsems) {
            return -22;
        }
        return (long)g_sysv_sem[idx].vals[semnum];
    }
    if (cmd == BFREE_SEM_SETVAL) {
        if (semnum < 0 || semnum >= g_sysv_sem[idx].nsems) {
            return -22;
        }
        g_sysv_sem[idx].vals[semnum] = (int)arg;
        return 0;
    }
    return 0;
}

/* SysV msg thin (msgget 68, msgsnd 69, msgrcv 70, msgctl 71) */
#define BFREE_MAX_SYSV_MSG 4
#define BFREE_MSG_PAYLOAD  240
typedef struct {
    int used;
    int has_msg;
    long mtype;
    size_t len;
    unsigned char data[BFREE_MSG_PAYLOAD];
} bfree_sysv_msg_t;
static bfree_sysv_msg_t g_sysv_msg[BFREE_MAX_SYSV_MSG];

static long sys_linux_msgget(long key, long msgflg)
{
    int i;

    (void)key;
    (void)msgflg;
    for (i = 0; i < BFREE_MAX_SYSV_MSG; ++i) {
        if (!g_sysv_msg[i].used) {
            g_sysv_msg[i].used = 1;
            g_sysv_msg[i].has_msg = 0;
            g_sysv_msg[i].mtype = 0;
            g_sysv_msg[i].len = 0;
            return (long)(i + 1);
        }
    }
    return -28;
}

static long sys_linux_msgsnd(long msqid, long msgp, long msgsz, long msgflg)
{
    int idx = (int)msqid - 1;
    long mtype;

    (void)msgflg;
    if (idx < 0 || idx >= BFREE_MAX_SYSV_MSG || !g_sysv_msg[idx].used) {
        return -22;
    }
    if (msgsz < 0 || msgsz > (long)BFREE_MSG_PAYLOAD) {
        return -22;
    }
    if (msgp == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)msgp,
                                            8ULL + (uint64_t)msgsz)) {
        return -14;
    }
    mtype = *(const long *)(uintptr_t)msgp;
    if (mtype <= 0) {
        return -22;
    }
    g_sysv_msg[idx].mtype = mtype;
    g_sysv_msg[idx].len = (size_t)msgsz;
    if (msgsz > 0) {
        memcpy(g_sysv_msg[idx].data,
               (const void *)(uintptr_t)(msgp + 8), (size_t)msgsz);
    }
    g_sysv_msg[idx].has_msg = 1;
    return 0;
}

static long sys_linux_msgrcv(long msqid, long msgp, long msgsz, long msgtyp,
                             long msgflg)
{
    int idx = (int)msqid - 1;
    size_t copy;

    (void)msgtyp;
    (void)msgflg;
    if (idx < 0 || idx >= BFREE_MAX_SYSV_MSG || !g_sysv_msg[idx].used) {
        return -22;
    }
    if (!g_sysv_msg[idx].has_msg) {
        return -42; /* ENOMSG soft */
    }
    if (msgsz < 0) {
        return -22;
    }
    if (msgp == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)msgp,
                                            8ULL + (uint64_t)msgsz)) {
        return -14;
    }
    copy = g_sysv_msg[idx].len;
    if (copy > (size_t)msgsz) {
        copy = (size_t)msgsz;
    }
    *(long *)(uintptr_t)msgp = g_sysv_msg[idx].mtype;
    if (copy > 0) {
        memcpy((void *)(uintptr_t)(msgp + 8), g_sysv_msg[idx].data, copy);
    }
    g_sysv_msg[idx].has_msg = 0;
    g_sysv_msg[idx].len = 0;
    return (long)copy;
}

static long sys_linux_msgctl(long msqid, long cmd, long buf)
{
    int idx = (int)msqid - 1;

    (void)buf;
    if (idx < 0 || idx >= BFREE_MAX_SYSV_MSG || !g_sysv_msg[idx].used) {
        return -22;
    }
    if (cmd == BFREE_IPC_RMID) {
        g_sysv_msg[idx].used = 0;
        g_sysv_msg[idx].has_msg = 0;
        g_sysv_msg[idx].len = 0;
        return 0;
    }
    return 0;
}

/* sched_setattr 314 / sched_getattr 315 */
static long sys_linux_sched_setattr(long pid, long attr_ptr, long flags)
{
    (void)pid;
    (void)flags;
    if (attr_ptr != 0 && !bfree_user_ptr_mapped(attr_ptr)) {
        return -14;
    }
    return 0;
}

static long sys_linux_sched_getattr(long pid, long attr_ptr, long size, long flags)
{
    uint32_t *u32;
    size_t i;
    uint8_t *p;

    (void)pid;
    (void)flags;
    if (size < 48) {
        return -22;
    }
    if (attr_ptr == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)attr_ptr,
                                                (uint64_t)size)) {
        return -14;
    }
    p = (uint8_t *)(uintptr_t)attr_ptr;
    for (i = 0; i < (size_t)size; ++i) {
        p[i] = 0;
    }
    u32 = (uint32_t *)(uintptr_t)attr_ptr;
    u32[0] = (uint32_t)size; /* size */
    u32[1] = 0;              /* SCHED_NORMAL */
    return 0;
}

/* adjtimex 159 / clock_adjtime 305 — return TIME_OK (0), zero modes/status */
static long sys_linux_adjtimex(long buf)
{
    uint8_t *p;
    size_t i;

    if (buf == 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    /* struct timex is large; zero first 128 bytes (modes/offset/status/…) */
    p = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < 128U; ++i) {
        if (!bfree_user_ptr_mapped(buf + (long)i)) {
            break;
        }
        p[i] = 0;
    }
    return 0; /* TIME_OK */
}

static long sys_linux_clock_adjtime(long clockid, long buf)
{
    (void)clockid;
    return sys_linux_adjtimex(buf);
}

static long sys_linux_quotactl(long cmd, long special, long id, long addr)
{
    (void)cmd;
    (void)special;
    (void)id;
    (void)addr;
    return 0;
}

/* keyring thin: add_key 248 / request_key 249 / keyctl 250 */
static int g_guest_key_next = 1;

static long sys_linux_add_key(long type, long desc, long payload, long plen,
                              long keyring)
{
    (void)type;
    (void)desc;
    (void)payload;
    (void)plen;
    (void)keyring;
    if (g_guest_key_next <= 0) {
        g_guest_key_next = 1;
    }
    return (long)g_guest_key_next++;
}

static long sys_linux_request_key(long type, long desc, long callout, long dest)
{
    (void)type;
    (void)desc;
    (void)callout;
    (void)dest;
    if (g_guest_key_next <= 0) {
        g_guest_key_next = 1;
    }
    return (long)g_guest_key_next++;
}

static long sys_linux_keyctl(long cmd, long arg2, long arg3, long arg4, long arg5)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    /* KEYCTL_READ=11 and others: soft success */
    if (cmd == 11) {
        return 0;
    }
    return 0;
}

/* perf_event_open 298 — published anon fd */
#define BFREE_PERF_FD_BASE 98200
#define BFREE_MAX_PERF     8
static int g_perf_used[BFREE_MAX_PERF];

static void bfree_perf_release(int resolved)
{
    int slot = resolved - BFREE_PERF_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_PERF) {
        g_perf_used[slot] = 0;
    }
}

static long sys_linux_perf_event_open(long attr, long pid, long cpu, long group_fd,
                                      long flags)
{
    int i;

    (void)attr;
    (void)pid;
    (void)cpu;
    (void)group_fd;
    (void)flags;
    for (i = 0; i < BFREE_MAX_PERF; ++i) {
        if (!g_perf_used[i]) {
            int pub;
            g_perf_used[i] = 1;
            pub = bfree_guest_fd_publish(BFREE_PERF_FD_BASE + i);
            if (pub < 0) {
                g_perf_used[i] = 0;
                return pub;
            }
            return pub;
        }
    }
    return -24;
}

static long sys_linux_epoll_pwait2(long epfd, long events, long maxevents,
                                   long timeout_ts, long sigmask)
{
    long timeout_ms = -1;

    (void)sigmask;
    if (timeout_ts != 0) {
        if (!bfree_user_ptr_mapped(timeout_ts)) {
            return -14;
        }
        {
            /* struct timespec { time_t tv_sec; long tv_nsec; } */
            int64_t sec = ((const int64_t *)(uintptr_t)timeout_ts)[0];
            int64_t nsec = ((const int64_t *)(uintptr_t)timeout_ts)[1];
            if (sec < 0 || nsec < 0) {
                return -22;
            }
            if (sec > 86400) {
                sec = 86400;
            }
            timeout_ms = (long)(sec * 1000 + nsec / 1000000);
        }
    }
    return sys_linux_epoll_wait(epfd, events, maxevents, timeout_ms);
}

static long sys_linux_futex_waitv(long waiters, long nr, long flags, long timeout,
                                  long clockid)
{
    (void)waiters;
    (void)flags;
    (void)timeout;
    (void)clockid;
    if (nr < 0) {
        return -22;
    }
    return 0;
}

/* mount API: open_tree 428 / move_mount 429 / fsopen 430 / fsconfig 431 / fsmount 432 */
#define BFREE_FSCTX_FD_BASE 98300
#define BFREE_MAX_FSCTX     8
static int g_fsctx_used[BFREE_MAX_FSCTX];

static void bfree_fsctx_release(int resolved)
{
    int slot = resolved - BFREE_FSCTX_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_FSCTX) {
        g_fsctx_used[slot] = 0;
    }
}

static long bfree_fsctx_alloc_fd(void)
{
    int i;

    for (i = 0; i < BFREE_MAX_FSCTX; ++i) {
        if (!g_fsctx_used[i]) {
            int pub;
            g_fsctx_used[i] = 1;
            pub = bfree_guest_fd_publish(BFREE_FSCTX_FD_BASE + i);
            if (pub < 0) {
                g_fsctx_used[i] = 0;
                return pub;
            }
            return pub;
        }
    }
    return -24;
}

static long sys_linux_fsopen(long fsname, long flags)
{
    (void)fsname;
    (void)flags;
    return bfree_fsctx_alloc_fd();
}

static long sys_linux_fsconfig(long fd, long cmd, long key, long value, long aux)
{
    int resolved = bfree_guest_fd_resolve((int)fd);
    int slot = resolved - BFREE_FSCTX_FD_BASE;

    (void)cmd;
    (void)key;
    (void)value;
    (void)aux;
    if (slot < 0 || slot >= BFREE_MAX_FSCTX || !g_fsctx_used[slot]) {
        return -9;
    }
    return 0;
}

static long sys_linux_fsmount(long fd, long flags, long attr_flags)
{
    int resolved = bfree_guest_fd_resolve((int)fd);
    int slot = resolved - BFREE_FSCTX_FD_BASE;

    (void)flags;
    (void)attr_flags;
    if (slot < 0 || slot >= BFREE_MAX_FSCTX || !g_fsctx_used[slot]) {
        return -9;
    }
    return bfree_fsctx_alloc_fd();
}

static long sys_linux_open_tree(long dfd, long path, long flags)
{
    (void)dfd;
    (void)path;
    (void)flags;
    return bfree_fsctx_alloc_fd();
}

static long sys_linux_move_mount(long from_dfd, long from_path, long to_dfd,
                                 long to_path, long flags)
{
    (void)from_dfd;
    (void)from_path;
    (void)to_dfd;
    (void)to_path;
    (void)flags;
    return 0;
}

/* ========== Section S soft ABI holes ========== */

#define BFREE_IOURING_FD_BASE   98400
#define BFREE_MAX_IOURING       4
#define BFREE_UFFD_FD_BASE      98500
#define BFREE_MAX_UFFD          4
#define BFREE_LANDLOCK_FD_BASE  98600
#define BFREE_MAX_LANDLOCK      4

static int g_iouring_used[BFREE_MAX_IOURING];
static int g_uffd_used[BFREE_MAX_UFFD];
static int g_landlock_used[BFREE_MAX_LANDLOCK];

static void bfree_iouring_release(int resolved)
{
    int slot = resolved - BFREE_IOURING_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_IOURING) {
        g_iouring_used[slot] = 0;
    }
}

static void bfree_uffd_release(int resolved)
{
    int slot = resolved - BFREE_UFFD_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_UFFD) {
        g_uffd_used[slot] = 0;
    }
}

static void bfree_landlock_release(int resolved)
{
    int slot = resolved - BFREE_LANDLOCK_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_LANDLOCK) {
        g_landlock_used[slot] = 0;
    }
}

static long bfree_soft_anon_fd_alloc(int *used, int max, int base)
{
    int i;

    for (i = 0; i < max; ++i) {
        if (!used[i]) {
            int pub;

            used[i] = 1;
            pub = bfree_guest_fd_publish(base + i);
            if (pub < 0) {
                used[i] = 0;
                return pub;
            }
            return pub;
        }
    }
    return -24;
}

/* io_uring_setup 425 / enter 426 / register 427 */
static long sys_linux_io_uring_setup(long entries, long params)
{
    (void)entries;
    (void)params;
    return bfree_soft_anon_fd_alloc(g_iouring_used, BFREE_MAX_IOURING,
                                    BFREE_IOURING_FD_BASE);
}

static long sys_linux_io_uring_enter(long fd, long to_submit, long min_complete,
                                     long flags, long sig)
{
    (void)fd;
    (void)to_submit;
    (void)min_complete;
    (void)flags;
    (void)sig;
    return 0;
}

static long sys_linux_io_uring_register(long fd, long opcode, long arg, long nr_args)
{
    (void)fd;
    (void)opcode;
    (void)arg;
    (void)nr_args;
    return 0;
}

/* userfaultfd 323 */
static long sys_linux_userfaultfd(long flags)
{
    (void)flags;
    return bfree_soft_anon_fd_alloc(g_uffd_used, BFREE_MAX_UFFD, BFREE_UFFD_FD_BASE);
}

/* landlock_create_ruleset 444 / add_rule 445 / restrict_self 446 */
static long sys_linux_landlock_create_ruleset(long attr, long size, long flags)
{
    (void)attr;
    (void)size;
    (void)flags;
    return bfree_soft_anon_fd_alloc(g_landlock_used, BFREE_MAX_LANDLOCK,
                                    BFREE_LANDLOCK_FD_BASE);
}

static long sys_linux_landlock_add_rule(long ruleset_fd, long rule_type, long rule_attr,
                                        long flags)
{
    (void)ruleset_fd;
    (void)rule_type;
    (void)rule_attr;
    (void)flags;
    return 0;
}

static long sys_linux_landlock_restrict_self(long ruleset_fd, long flags)
{
    (void)ruleset_fd;
    (void)flags;
    return 0;
}

/* seccomp 317 */
static long sys_linux_seccomp(long op, long flags, long uargs)
{
    (void)op;
    (void)flags;
    (void)uargs;
    return 0;
}

/* bpf 321 */
static long sys_linux_bpf(long cmd, long attr, long size)
{
    (void)cmd;
    (void)attr;
    (void)size;
    return 0;
}

/* ptrace 101 — soft TRACEME / any request */
static long sys_linux_ptrace(long request, long pid, long addr, long data)
{
    (void)request;
    (void)pid;
    (void)addr;
    (void)data;
    return 0;
}

/* syslog 103 — SYSLOG_ACTION_READ_ALL=3 or soft 0 */
static long sys_linux_syslog(long type, long buf, long len)
{
    static const char msg[] = "bfree\n";
    size_t n = sizeof(msg) - 1U;
    size_t i;
    char *dst;

    if (type != 3 && type != 0) {
        return 0;
    }
    if (buf == 0 || len <= 0) {
        return (long)n;
    }
    if (!bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if ((size_t)len < n) {
        n = (size_t)len;
    }
    dst = (char *)(uintptr_t)buf;
    for (i = 0; i < n; ++i) {
        dst[i] = msg[i];
    }
    return (long)n;
}

/* acct 163 / swapon 167 / swapoff 168 */
static long sys_linux_acct(long filename)
{
    (void)filename;
    return 0;
}

static long sys_linux_swapon(long specialfile, long swap_flags)
{
    (void)specialfile;
    (void)swap_flags;
    return 0;
}

static long sys_linux_swapoff(long specialfile)
{
    (void)specialfile;
    return 0;
}

/* init_module 175 / delete_module 176 / finit_module 313 */
static long sys_linux_init_module(long module_image, long len, long param_values)
{
    (void)module_image;
    (void)len;
    (void)param_values;
    return 0;
}

static long sys_linux_delete_module(long name, long flags)
{
    (void)name;
    (void)flags;
    return 0;
}

static long sys_linux_finit_module(long fd, long param_values, long flags)
{
    (void)fd;
    (void)param_values;
    (void)flags;
    return 0;
}

/* reboot 169 — validate magic, never reset */
#define BFREE_LINUX_REBOOT_MAGIC1 0xfee1deadL
#define BFREE_LINUX_REBOOT_MAGIC2 672274793L

static long sys_linux_reboot(long magic, long magic2, long cmd, long arg)
{
    (void)cmd;
    (void)arg;
    if (magic != BFREE_LINUX_REBOOT_MAGIC1 || magic2 != BFREE_LINUX_REBOOT_MAGIC2) {
        return -22;
    }
    return 0;
}

/* fspick 433 — reuse fsctx anon fd; mount_setattr 442 soft 0 */
static long sys_linux_fspick(long dfd, long path, long flags)
{
    (void)dfd;
    (void)path;
    (void)flags;
    return bfree_fsctx_alloc_fd();
}

static long sys_linux_mount_setattr(long dfd, long path, long flags, long uattr,
                                    long usize)
{
    (void)dfd;
    (void)path;
    (void)flags;
    (void)uattr;
    (void)usize;
    return 0;
}

/* personality 135 / modify_ldt 154 */
static long sys_linux_personality(long persona)
{
    (void)persona;
    return 0;
}

static long sys_linux_modify_ldt(long func, long ptr, long bytecount)
{
    (void)func;
    (void)ptr;
    (void)bytecount;
    return 0;
}

static long sys_linux_process_madvise(long pidfd, long iovec, long vlen, long advice,
                                      long flags)
{
    (void)pidfd;
    (void)iovec;
    (void)vlen;
    (void)advice;
    (void)flags;
    return 0;
}

/* pkey_mprotect 329 / pkey_alloc 330 / pkey_free 331 */
static uint16_t g_pkey_bits; /* bits 1..15 */

static long sys_linux_pkey_alloc(long flags, long access_rights)
{
    int i;

    (void)flags;
    (void)access_rights;
    for (i = 1; i <= 15; ++i) {
        if ((g_pkey_bits & (uint16_t)(1U << i)) == 0) {
            g_pkey_bits |= (uint16_t)(1U << i);
            return (long)i;
        }
    }
    return -28;
}

static long sys_linux_pkey_free(long pkey)
{
    if (pkey < 1 || pkey > 15) {
        return -22;
    }
    g_pkey_bits &= (uint16_t)~(1U << (int)pkey);
    return 0;
}

static long sys_linux_pkey_mprotect(long addr, long len, long prot, long pkey)
{
    (void)addr;
    (void)len;
    (void)prot;
    (void)pkey;
    return 0;
}

/* cachestat 451 / statmount 457 / listmount 458 */
static long sys_linux_cachestat(long fd, long cstat_range, long cstat, long flags)
{
    uint8_t *p;
    size_t i;

    (void)fd;
    (void)cstat_range;
    (void)flags;
    if (cstat == 0 || !bfree_user_ptr_mapped(cstat)) {
        return -14;
    }
    p = (uint8_t *)(uintptr_t)cstat;
    for (i = 0; i < 32U; ++i) {
        if (!bfree_user_ptr_mapped(cstat + (long)i)) {
            break;
        }
        p[i] = 0;
    }
    return 0;
}

static long sys_linux_statmount(long req, long buf, long bufsize, long flags)
{
    (void)req;
    (void)flags;
    if (buf != 0 && bufsize > 0 && bfree_user_ptr_mapped(buf)) {
        uint8_t *p = (uint8_t *)(uintptr_t)buf;
        size_t n = (size_t)bufsize;
        size_t i;
        if (n > 64U) {
            n = 64U;
        }
        for (i = 0; i < n; ++i) {
            p[i] = 0;
        }
        if (n >= 8U) {
            /* size field soft */
            *(uint32_t *)(uintptr_t)buf = (uint32_t)n;
        }
    }
    return 0;
}

static long sys_linux_listmount(long req, long mnt_ids, long nr, long flags)
{
    (void)req;
    (void)mnt_ids;
    (void)nr;
    (void)flags;
    return 0;
}

static long sys_linux_kcmp(long pid1, long pid2, long type, long idx1, long idx2)
{
    (void)type;
    (void)idx1;
    (void)idx2;
    (void)pid1;
    (void)pid2;
    return 0; /* soft: treat as same */
}

/* path xattr: setxattr 188 / getxattr 191 / listxattr 194 / removexattr 197
 * (+ l* aliases 189/192/195/198) — reuse vfile xattr keyed by /tmp name */
#ifndef BFREE_ENODATA
#define BFREE_ENODATA 61
#endif
static bfree_guest_vfile_t *bfree_guest_vfile_from_xpath(long path_ptr)
{
    char path[256];
    char vname[64];

    if (path_ptr == 0 || copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return 0;
    }
    bfree_guest_path_absolutize(path, sizeof(path));
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return 0;
    }
    return bfree_guest_vfile_find_by_name(vname);
}

static long sys_linux_setxattr(long path_ptr, long name_ptr, long value_ptr,
                               long size, long flags)
{
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_xpath(path_ptr);
    char name[32];
    size_t n;

    (void)flags;
    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (name[0] == '\0') {
        return -22;
    }
    if (size < 0 || size > (long)sizeof(vf->xattr_value)) {
        return -34;
    }
    if (size > 0 &&
        (value_ptr == 0 ||
         !bfree_user_buf_mapped((uint64_t)(uintptr_t)value_ptr, (uint64_t)size))) {
        return -14;
    }
    for (n = 0; n < sizeof(vf->xattr_name); ++n) {
        vf->xattr_name[n] = name[n];
        if (name[n] == '\0') {
            break;
        }
    }
    vf->xattr_name[sizeof(vf->xattr_name) - 1] = '\0';
    if (size > 0) {
        memcpy(vf->xattr_value, (const void *)(uintptr_t)value_ptr, (size_t)size);
    }
    vf->xattr_len = (size_t)size;
    return 0;
}

static long sys_linux_getxattr(long path_ptr, long name_ptr, long value_ptr,
                               long size)
{
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_xpath(path_ptr);
    char name[32];
    size_t n;

    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (vf->xattr_name[0] == '\0' || strcmp(vf->xattr_name, name) != 0) {
        return -BFREE_ENODATA;
    }
    if (size == 0) {
        return (long)vf->xattr_len;
    }
    if (size < (long)vf->xattr_len) {
        return -34;
    }
    if (value_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)value_ptr, vf->xattr_len)) {
        return -14;
    }
    if (vf->xattr_len > 0) {
        memcpy((void *)(uintptr_t)value_ptr, vf->xattr_value, vf->xattr_len);
    }
    return (long)vf->xattr_len;
}

static long sys_linux_listxattr(long path_ptr, long list_ptr, long size)
{
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_xpath(path_ptr);
    size_t namelen;

    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (vf->xattr_name[0] == '\0') {
        return 0;
    }
    namelen = 0;
    while (vf->xattr_name[namelen] != '\0' &&
           namelen + 1U < sizeof(vf->xattr_name)) {
        ++namelen;
    }
    namelen += 1;
    if (size == 0) {
        return (long)namelen;
    }
    if (size < (long)namelen) {
        return -34;
    }
    if (list_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)list_ptr, namelen)) {
        return -14;
    }
    memcpy((void *)(uintptr_t)list_ptr, vf->xattr_name, namelen);
    return (long)namelen;
}

static long sys_linux_removexattr(long path_ptr, long name_ptr)
{
    bfree_guest_vfile_t *vf = bfree_guest_vfile_from_xpath(path_ptr);
    char name[32];
    size_t n;

    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (vf->xattr_name[0] == '\0' || strcmp(vf->xattr_name, name) != 0) {
        return -BFREE_ENODATA;
    }
    vf->xattr_name[0] = '\0';
    vf->xattr_len = 0;
    return 0;
}

static long bfree_copy_iovecs(long dst_iov, long dstcnt, long src_iov, long srccnt)
{
    long di = 0, si = 0;
    uint64_t d_off = 0, s_off = 0;
    long total = 0;

    if (dstcnt <= 0 || srccnt <= 0 || dstcnt > 1024 || srccnt > 1024) {
        return -22;
    }
    if (!bfree_user_ptr_mapped(dst_iov) || !bfree_user_ptr_mapped(src_iov)) {
        return -14;
    }
    while (di < dstcnt && si < srccnt) {
        bfree_linux_iovec_t dvec, svec;
        uint64_t d_addr = (uint64_t)(uintptr_t)(dst_iov + di * (long)sizeof(dvec));
        uint64_t s_addr = (uint64_t)(uintptr_t)(src_iov + si * (long)sizeof(svec));
        uint64_t d_rem, s_rem, n;
        uint8_t *dp;
        const uint8_t *sp;

        if (!bfree_user_buf_mapped(d_addr, sizeof(dvec)) ||
            !bfree_user_buf_mapped(s_addr, sizeof(svec))) {
            return -14;
        }
        memcpy(&dvec, (const void *)(uintptr_t)d_addr, sizeof(dvec));
        memcpy(&svec, (const void *)(uintptr_t)s_addr, sizeof(svec));
        if (d_off >= dvec.iov_len) {
            d_off = 0;
            ++di;
            continue;
        }
        if (s_off >= svec.iov_len) {
            s_off = 0;
            ++si;
            continue;
        }
        d_rem = dvec.iov_len - d_off;
        s_rem = svec.iov_len - s_off;
        n = d_rem < s_rem ? d_rem : s_rem;
        if (n == 0) {
            if (d_rem == 0) {
                d_off = 0;
                ++di;
            }
            if (s_rem == 0) {
                s_off = 0;
                ++si;
            }
            continue;
        }
        if (!bfree_user_buf_mapped(dvec.iov_base + d_off, n) ||
            !bfree_user_buf_mapped(svec.iov_base + s_off, n)) {
            return -14;
        }
        dp = (uint8_t *)(uintptr_t)(dvec.iov_base + d_off);
        sp = (const uint8_t *)(uintptr_t)(svec.iov_base + s_off);
        memcpy(dp, sp, (size_t)n);
        total += (long)n;
        d_off += n;
        s_off += n;
    }
    return total;
}

static long sys_linux_process_vm_readv(long pid, long local_iov, long liovcnt,
                                       long remote_iov, long riovcnt, long flags)
{
    (void)flags;
    if (pid != sys_linux_getpid() && pid != 0) {
        return -3; /* ESRCH — self-only soft */
    }
    return bfree_copy_iovecs(local_iov, liovcnt, remote_iov, riovcnt);
}

static long sys_linux_process_vm_writev(long pid, long local_iov, long liovcnt,
                                        long remote_iov, long riovcnt, long flags)
{
    (void)flags;
    if (pid != sys_linux_getpid() && pid != 0) {
        return -3;
    }
    return bfree_copy_iovecs(remote_iov, riovcnt, local_iov, liovcnt);
}

/* Linux 435: clone3 → wrap existing clone. */
static long sys_linux_clone3(long args_ptr, long size)
{
    typedef struct {
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
    } bfree_clone_args_t;
    bfree_clone_args_t a;
    long flags;
    long stack;

    if (size < 64 || args_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)args_ptr,
                               (uint64_t)((size > (long)sizeof(a)) ? (long)sizeof(a) : size))) {
        return -22;
    }
    memset(&a, 0, sizeof(a));
    memcpy(&a, (const void *)(uintptr_t)args_ptr,
           (size_t)((size < (long)sizeof(a)) ? size : (long)sizeof(a)));
    flags = (long)(a.flags | (a.exit_signal & 0xffULL));
    stack = (long)a.stack;
    if (a.stack_size != 0ULL && a.stack != 0ULL) {
        stack = (long)(a.stack + a.stack_size);
    }
    return sys_linux_clone(flags, stack, (long)a.parent_tid, (long)a.child_tid,
                           (long)a.tls);
}

/* fanotify thin — like inotify */
#define BFREE_FANOTIFY_FD_BASE 98100
#define BFREE_MAX_FANOTIFY     4
static struct {
    int used;
} g_fanotify[BFREE_MAX_FANOTIFY];

static void bfree_fanotify_release(int resolved)
{
    int slot = resolved - BFREE_FANOTIFY_FD_BASE;

    if (slot >= 0 && slot < BFREE_MAX_FANOTIFY) {
        g_fanotify[slot].used = 0;
    }
}

static long sys_linux_fanotify_init(long flags, long event_f_flags)
{
    int i;

    (void)flags;
    (void)event_f_flags;
    for (i = 0; i < BFREE_MAX_FANOTIFY; ++i) {
        if (!g_fanotify[i].used) {
            int pub;

            g_fanotify[i].used = 1;
            pub = bfree_guest_fd_publish(BFREE_FANOTIFY_FD_BASE + i);
            if (pub < 0) {
                g_fanotify[i].used = 0;
                return pub;
            }
            return pub;
        }
    }
    return -24;
}

static long sys_linux_fanotify_mark(long fanotify_fd, long flags, long mask,
                                    long dirfd, long pathname)
{
    int resolved = bfree_guest_fd_resolve((int)fanotify_fd);
    int slot = resolved - BFREE_FANOTIFY_FD_BASE;

    (void)flags;
    (void)mask;
    (void)dirfd;
    (void)pathname;
    if (slot < 0 || slot >= BFREE_MAX_FANOTIFY || !g_fanotify[slot].used) {
        return -9;
    }
    return 0;
}

/* name_to_handle_at / open_by_handle_at — side table of paths */
#define BFREE_MAX_FHANDLE 8
static struct {
    int used;
    uint8_t handle[8];
    char path[96];
} g_fhandles[BFREE_MAX_FHANDLE];
static unsigned g_fhandle_seq;

static long sys_linux_name_to_handle_at(long dfd, long path_ptr, long handle_ptr,
                                        long mount_id_ptr, long flags)
{
    char path[256];
    long path_err;
    typedef struct {
        unsigned int handle_bytes;
        int handle_type;
    } bfree_file_handle_hdr_t;
    bfree_file_handle_hdr_t *hdr;
    uint8_t *f_handle;
    unsigned need;
    int i;
    unsigned seq;

    (void)flags;
    if (handle_ptr == 0 || !bfree_user_ptr_mapped(handle_ptr)) {
        return -14;
    }
    hdr = (bfree_file_handle_hdr_t *)(uintptr_t)handle_ptr;
    need = hdr->handle_bytes;
    if (need < 8U) {
        hdr->handle_bytes = 8U;
        return -75; /* EOVERFLOW — tell user needed size */
    }
    if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)handle_ptr,
                               sizeof(*hdr) + 8U)) {
        return -14;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    seq = ++g_fhandle_seq;
    for (i = 0; i < BFREE_MAX_FHANDLE; ++i) {
        if (!g_fhandles[i].used) {
            size_t j;
            g_fhandles[i].used = 1;
            g_fhandles[i].handle[0] = (uint8_t)(seq & 0xffU);
            g_fhandles[i].handle[1] = (uint8_t)((seq >> 8) & 0xffU);
            g_fhandles[i].handle[2] = (uint8_t)((seq >> 16) & 0xffU);
            g_fhandles[i].handle[3] = (uint8_t)((seq >> 24) & 0xffU);
            g_fhandles[i].handle[4] = (uint8_t)i;
            g_fhandles[i].handle[5] = 0xBF;
            g_fhandles[i].handle[6] = 0xEE;
            g_fhandles[i].handle[7] = 0x01;
            for (j = 0; j + 1U < sizeof(g_fhandles[i].path) && path[j]; ++j) {
                g_fhandles[i].path[j] = path[j];
            }
            g_fhandles[i].path[j] = '\0';
            hdr->handle_bytes = 8U;
            hdr->handle_type = 1;
            f_handle = (uint8_t *)(uintptr_t)(handle_ptr + (long)sizeof(*hdr));
            for (j = 0; j < 8U; ++j) {
                f_handle[j] = g_fhandles[i].handle[j];
            }
            if (mount_id_ptr != 0 && bfree_user_ptr_mapped(mount_id_ptr)) {
                *(int *)(uintptr_t)mount_id_ptr = 1;
            }
            return 0;
        }
    }
    return -28;
}

static long sys_linux_open_by_handle_at(long mount_fd, long handle_ptr, long flags)
{
    typedef struct {
        unsigned int handle_bytes;
        int handle_type;
    } bfree_file_handle_hdr_t;
    bfree_file_handle_hdr_t *hdr;
    uint8_t *f_handle;
    int i;
    int cloexec;

    (void)mount_fd;
    if (handle_ptr == 0 || !bfree_user_ptr_mapped(handle_ptr)) {
        return -14;
    }
    hdr = (bfree_file_handle_hdr_t *)(uintptr_t)handle_ptr;
    if (hdr->handle_bytes < 8U) {
        return -22;
    }
    if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)handle_ptr,
                               sizeof(*hdr) + 8U)) {
        return -14;
    }
    f_handle = (uint8_t *)(uintptr_t)(handle_ptr + (long)sizeof(*hdr));
    cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    for (i = 0; i < BFREE_MAX_FHANDLE; ++i) {
        int same = 1;
        int b;

        if (!g_fhandles[i].used) {
            continue;
        }
        for (b = 0; b < 8; ++b) {
            if (g_fhandles[i].handle[b] != f_handle[b]) {
                same = 0;
                break;
            }
        }
        if (same) {
            const char *path = g_fhandles[i].path;
            int pub = -9;

            if (path[0] == '/' && path[1] == '\0') {
                pub = bfree_guest_fd_publish(BFREE_GUEST_ROOT_DIR_FD);
            } else if (strcmp(path, "/tmp") == 0) {
                pub = bfree_guest_fd_publish(BFREE_GUEST_TMP_DIR_FD);
            } else if (strncmp(path, "/tmp/", 5) == 0 && path[5] != '\0') {
                bfree_guest_vfile_t *vf =
                    bfree_guest_vfile_find_by_name(path + 5);
                if (vf) {
                    int target = (int)BFREE_GUEST_VFILE_FD_BASE +
                                 (int)(vf - g_guest_vfiles);
                    pub = bfree_guest_vfile_publish_open(target, (int)flags, 0);
                }
                if (pub < 0) {
                    pub = bfree_guest_fd_publish(BFREE_GUEST_TMP_DIR_FD);
                }
            } else {
                pub = bfree_guest_fd_publish(BFREE_GUEST_TMP_DIR_FD);
            }
            if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    {
        int pub = bfree_guest_fd_publish(BFREE_GUEST_TMP_DIR_FD);
        if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
            g_guest_fd_cloexec[pub] = 1;
        }
        return pub;
    }
}

/* memfd_secret — like memfd_create with distinct name prefix */
static unsigned g_guest_memfd_secret_seq;

static long sys_linux_memfd_secret(long flags)
{
    char vname[48];
    size_t i = 0;
    int target;
    int pub;
    int cloexec;
    bfree_guest_vfile_t *dir;
    unsigned seq;

    cloexec = ((unsigned long)flags & (unsigned long)BFREE_MFD_CLOEXEC) != 0UL;
    seq = g_guest_memfd_secret_seq++;
    vname[0] = 'm';
    vname[1] = 'e';
    vname[2] = 'm';
    vname[3] = 'f';
    vname[4] = 'd';
    vname[5] = '_';
    vname[6] = 's';
    vname[7] = 'e';
    vname[8] = 'c';
    vname[9] = 'r';
    vname[10] = 'e';
    vname[11] = 't';
    vname[12] = '/';
    {
        char num[16];
        size_t n = 0;
        unsigned v = seq;

        if (v == 0U) {
            num[n++] = '0';
        } else {
            while (v > 0U && n < sizeof(num)) {
                num[n++] = (char)('0' + (v % 10U));
                v /= 10U;
            }
        }
        while (n > 0U && i + 14U < sizeof(vname)) {
            vname[13 + i] = num[--n];
            ++i;
        }
    }
    vname[13 + i] = '\0';

    dir = bfree_guest_vfile_find_by_name("memfd_secret");
    if (!dir) {
        int dfd = bfree_guest_vfile_alloc_slot("memfd_secret", 1);
        if (dfd < 0) {
            return dfd;
        }
        dir = bfree_guest_vfile_from_fd(dfd);
        if (dir) {
            dir->is_dir = 1;
        }
    }
    target = bfree_guest_vfile_alloc_slot(vname, 1);
    if (target < 0) {
        return target;
    }
    pub = bfree_guest_vfile_publish_open(target, 2 /* O_RDWR */, 0);
    if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_cloexec[pub] = 1;
    }
    return pub;
}

static long sys_linux_clock_settime(long clockid, long tp)
{
    typedef struct {
        long tv_sec;
        long tv_nsec;
    } bfree_ts_t;
    bfree_ts_t *ts;

    if (clockid != 0) { /* only CLOCK_REALTIME soft-store */
        return 0;
    }
    if (tp == 0 || !bfree_user_ptr_mapped(tp)) {
        return -14;
    }
    ts = (bfree_ts_t *)(uintptr_t)tp;
    if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
        return -22;
    }
    g_guest_realtime_sec = ts->tv_sec;
    g_guest_realtime_nsec = ts->tv_nsec;
    g_guest_realtime_override = 1;
    return 0;
}

static long sys_linux_settimeofday(long tv_ptr, long tz_ptr)
{
    typedef struct {
        long tv_sec;
        long tv_usec;
    } bfree_tv_t;
    bfree_tv_t *tv;

    (void)tz_ptr;
    if (tv_ptr == 0) {
        return 0;
    }
    if (!bfree_user_ptr_mapped(tv_ptr)) {
        return -14;
    }
    tv = (bfree_tv_t *)(uintptr_t)tv_ptr;
    g_guest_realtime_sec = tv->tv_sec;
    g_guest_realtime_nsec = tv->tv_usec * 1000L;
    g_guest_realtime_override = 1;
    return 0;
}

/* Linux 436: CLOSE_RANGE_CLOEXEC marks the range instead of closing it. */
#define BFREE_CLOSE_RANGE_CLOEXEC 4

static long sys_linux_close_range(long first, long last, long flags)
{
    long fd;

    if (first < 0 || last < first) {
        return -22;
    }
    if ((flags & ~(long)(BFREE_CLOSE_RANGE_CLOEXEC | 2)) != 0L) {
        return -22; /* only CLOEXEC and UNSHARE are known */
    }
    if (last >= BFREE_GUEST_FD_TABLE_SIZE) {
        last = BFREE_GUEST_FD_TABLE_SIZE - 1;
    }
    for (fd = first; fd <= last; ++fd) {
        /* Same open check as fstat/F_GETFD: resolve alone is not enough. */
        if (fd > 2 && fd < BFREE_GUEST_FD_TABLE_SIZE &&
            g_guest_fd_target[fd] < 0 && g_guest_fd_dup_save[fd] < 0) {
            continue;
        }
        if ((flags & BFREE_CLOSE_RANGE_CLOEXEC) != 0L) {
            g_guest_fd_cloexec[fd] = 1;
        } else {
            (void)sys_linux_close(fd);
        }
    }
    return 0;
}

/* Linux 437: struct open_how { u64 flags; u64 mode; u64 resolve; }. */
static long sys_linux_openat2(long dirfd, long path_ptr, long how_ptr,
                              long how_size)
{
    typedef struct {
        uint64_t flags;
        uint64_t mode;
        uint64_t resolve;
    } bfree_open_how_t;
    bfree_open_how_t how;
    const uint8_t *src;
    size_t n;

    if (how_size < 24) {
        return -22;
    }
    if (how_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)how_ptr, sizeof(how))) {
        return -14;
    }
    src = (const uint8_t *)(uintptr_t)how_ptr;
    for (n = 0; n < sizeof(how); ++n) {
        ((uint8_t *)&how)[n] = src[n];
    }
    /* RESOLVE_* is advisory here: every guest path is already sandboxed. */
    return sys_linux_openat(dirfd, path_ptr, (long)how.flags, (long)how.mode);
}

static void bfree_guest_trace_sc_num(long num)
{
    (void)num;
#if defined(BFREE_GUEST_SYSCALL_TRACE) && BFREE_GUEST_SYSCALL_TRACE
    static unsigned trace_count;
    char buf[20];
    int i = 0;
    unsigned long u = (unsigned long)num;

    if (trace_count >= 128U) {
        return;
    }
    ++trace_count;
    uart_puts("[SC] ");
    if (u == 0) {
        buf[i++] = '0';
    } else {
        char tmp[16];
        int j = 0;
        while (u > 0 && j < 16) {
            tmp[j++] = (char)('0' + (u % 10U));
            u /= 10U;
        }
        while (j > 0) {
            buf[i++] = tmp[--j];
        }
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    uart_puts(buf);
#endif
}




/* === H02 AS-copy / coop pipe concurrency === */
#ifndef BFREE_H02_AS_COPY_WIRED
#define BFREE_H02_AS_COPY_WIRED 1

static void bfree_coop_fd_snap_init(void)
{
    int i;
    bfree_guest_fd_ensure_init();
    for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        g_fd_snap_parent[i] = g_guest_fd_target[i];
        g_fd_snap_child[i] = g_guest_fd_target[i];
        g_fd_dup_save_snap_parent[i] = g_guest_fd_dup_save[i];
        g_fd_dup_save_snap_child[i] = g_guest_fd_dup_save[i];
    }
    g_coop_side = 1;
    g_coop_child_blocked = 0;
    g_coop_parent_started = 0;
}

static void bfree_coop_fd_switch_to(int side)
{
    int i;
    if (side == g_coop_side) {
        return;
    }
    if (g_coop_side == 1) {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_fd_snap_child[i] = g_guest_fd_target[i];
            g_fd_dup_save_snap_child[i] = g_guest_fd_dup_save[i];
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_parent[i];
            g_guest_fd_dup_save[i] = g_fd_dup_save_snap_parent[i];
        }
    } else {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_fd_snap_parent[i] = g_guest_fd_target[i];
            g_fd_dup_save_snap_parent[i] = g_guest_fd_dup_save[i];
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_child[i];
            g_guest_fd_dup_save[i] = g_fd_dup_save_snap_child[i];
        }
    }
    g_coop_side = side;
}

static void bfree_coop_save_child_user(void)
{
    g_coop_child_rcx = g_bfree_user_sysret_rcx;
    g_coop_child_r11 = g_bfree_user_sysret_r11;
    g_coop_child_rsp = g_bfree_user_sysret_rsp;
    g_coop_child_rbx = g_bfree_user_sysret_rbx;
    g_coop_child_rbp = g_bfree_user_sysret_rbp;
    g_coop_child_r12 = g_bfree_user_sysret_r12;
    g_coop_child_r13 = g_bfree_user_sysret_r13;
    g_coop_child_r14 = g_bfree_user_sysret_r14;
    g_coop_child_r15 = g_bfree_user_sysret_r15;
    g_coop_child_rdx = g_bfree_user_sysret_rdx;
}

static void bfree_coop_save_parent_user(void)
{
    g_coop_parent_rcx = g_bfree_user_sysret_rcx;
    g_coop_parent_r11 = g_bfree_user_sysret_r11;
    g_coop_parent_rsp = g_bfree_user_sysret_rsp;
    g_coop_parent_rbx = g_bfree_user_sysret_rbx;
    g_coop_parent_rbp = g_bfree_user_sysret_rbp;
    g_coop_parent_r12 = g_bfree_user_sysret_r12;
    g_coop_parent_r13 = g_bfree_user_sysret_r13;
    g_coop_parent_r14 = g_bfree_user_sysret_r14;
    g_coop_parent_r15 = g_bfree_user_sysret_r15;
    g_coop_parent_rdx = g_bfree_user_sysret_rdx;
}

/* Stage parent into fork_saved_* for FORK_PARENT/COOP (-4092/-4093). */
static void bfree_coop_publish_parent_resume(void)
{
    g_bfree_fork_saved_rbx = g_coop_parent_rbx;
    g_bfree_fork_saved_rbp = g_coop_parent_rbp;
    g_bfree_fork_saved_r12 = g_coop_parent_r12;
    g_bfree_fork_saved_r13 = g_coop_parent_r13;
    g_bfree_fork_saved_r14 = g_coop_parent_r14;
    g_bfree_fork_saved_r15 = g_coop_parent_r15;
    g_bfree_fork_saved_rdx = g_coop_parent_rdx;
    g_bfree_fork_saved_rcx = g_coop_parent_rcx;
    g_bfree_fork_saved_r11 = g_coop_parent_r11;
    g_bfree_fork_saved_rsp = g_coop_parent_rsp;
    g_bfree_sysret_exec_rsp = g_coop_parent_rsp;
    g_bfree_sysret_exec_rcx = g_coop_parent_rcx;
    g_bfree_sysret_exec_r11 = g_coop_parent_r11;
}

/* Stage child into fork_saved_* (entry.S COOP uses fork_saved RIP/RSP). */
static void bfree_coop_publish_child_resume(void)
{
    g_bfree_fork_saved_rbx = g_coop_child_rbx;
    g_bfree_fork_saved_rbp = g_coop_child_rbp;
    g_bfree_fork_saved_r12 = g_coop_child_r12;
    g_bfree_fork_saved_r13 = g_coop_child_r13;
    g_bfree_fork_saved_r14 = g_coop_child_r14;
    g_bfree_fork_saved_r15 = g_coop_child_r15;
    g_bfree_fork_saved_rdx = g_coop_child_rdx;
    g_bfree_fork_saved_rcx = g_coop_child_rcx;
    g_bfree_fork_saved_r11 = g_coop_child_r11;
    g_bfree_fork_saved_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
}

/* H02: flip CR3 with logical parent/child side. FORK_PARENT must not use exec_cr3. */
static void bfree_coop_as_switch_to(int side)
{
    page_table_t *pt;

    if (!knl_current_task) {
        return;
    }
    if (!bfree_process_child_has_private_as()) {
        return;
    }
    pt = (side == 0) ? bfree_process_parent_pt() : bfree_process_child_pt();
    if (!pt) {
        return;
    }
    if ((page_table_t *)knl_current_task->page_table_base == pt) {
        return;
    }
    knl_current_task->page_table_base = pt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pt) : "memory");
    /* Never leave a stale exec_cr3 for FORK_PARENT/COOP resume. */
    g_bfree_sysret_exec_cr3 = 0;
}

static void bfree_coop_arm_parent_resume(void)
{
    if (!g_coop_parent_started) {
        g_coop_parent_started = 1;
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
        return;
    }
    if (g_coop_parent_resume_mode == 2) {
        g_bfree_fork_saved_rcx = g_coop_parent_rcx;
        g_bfree_fork_parent_ret = g_coop_parent_resume_rax;
        g_coop_parent_resume_mode = 0;
        return;
    }
    if (g_coop_parent_rcx >= 2) {
        g_bfree_fork_saved_rcx = g_coop_parent_rcx - 2;
    }
    if (g_coop_parent_resume_mode == 1) {
        g_bfree_fork_parent_ret = g_coop_parent_resume_rax;
        g_coop_parent_resume_mode = 0;
    } else {
        g_bfree_fork_parent_ret = 0;
    }
}

static void bfree_coop_arm_child_resume(void)
{
    if (g_coop_child_resume_mode == 1 || g_coop_child_resume_mode == 2) {
        g_bfree_fork_parent_ret = g_coop_child_resume_rax;
        g_coop_child_resume_mode = 0;
    } else {
        g_bfree_fork_parent_ret = 0;
    }
}

static long bfree_coop_yield_to_parent(void)
{
    bfree_coop_save_child_user();
    if (g_coop_child_rcx >= 2) {
        g_coop_child_rcx -= 2;
    }
    g_coop_child_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_child_resume_mode = 1;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_publish_parent_resume();
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child(void)
{
    bfree_coop_save_parent_user();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)g_coop_cur_nr;
    g_coop_parent_resume_mode = 1;
    bfree_coop_fd_switch_to(1);
    if (g_guest_fork_saved_fsbase != 0) {
        if (knl_current_task != 0) {
            knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    }
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    bfree_coop_publish_child_resume();
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_parent_done(long ret)
{
    bfree_coop_save_child_user();
    g_coop_child_resume_rax = (uint64_t)(long)ret;
    g_coop_child_resume_mode = 2;
    g_coop_child_blocked = 1;
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_parent();
    }
    bfree_coop_fd_switch_to(0);
    bfree_coop_as_switch_to(0);
    bfree_coop_publish_parent_resume();
    bfree_coop_arm_parent_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child_done(long ret)
{
    bfree_coop_save_parent_user();
    if (g_guest_fork_was_as_copy) {
        bfree_guest_as_copy_switch_heap_to_child();
    }
    g_coop_parent_resume_rax = (uint64_t)(long)ret;
    g_coop_parent_resume_mode = 2;
    bfree_coop_fd_switch_to(1);
    bfree_coop_as_switch_to(1);
    g_coop_child_blocked = 0;
    bfree_coop_publish_child_resume();
    bfree_coop_arm_child_resume();
    return BFREE_SYSRET_COOP_SWITCH;
}

#endif /* BFREE_H02_AS_COPY_WIRED */

/* === restore soft bodies (compile-only; H02/H01 replace later) === */

/* === H01 rt_sigframe / CATCH deliver ===
 * D: fxsave blob + one nested CATCH queue; full glibc ucontext layout still approximate. */
#ifndef BFREE_H01_SIGFRAME_WIRED
#define BFREE_H01_SIGFRAME_WIRED 1

#ifndef BFREE_OFFSETOF
#define BFREE_OFFSETOF(type, member) __builtin_offsetof(type, member)
#endif

static int g_sig_deliver_sig;
static int g_sig_in_handler;
static int g_sig_nested_pending; /* D: one nested CATCH queued while in handler */
static int g_sig_mask_pushed;
static uint64_t g_sig_saved_rax;
static uint64_t g_sig_saved_rdi;
static uint64_t g_sig_saved_rsi;
static uint64_t g_sig_saved_rdx;
static uint64_t g_sig_saved_rbx;
static uint64_t g_sig_saved_rbp;
static uint64_t g_sig_saved_r12;
static uint64_t g_sig_saved_r13;
static uint64_t g_sig_saved_r14;
static uint64_t g_sig_saved_r15;
static uint64_t g_sig_saved_rip;
static uint64_t g_sig_saved_rsp;
static uint64_t g_sig_saved_rflags;
static uint64_t g_sig_saved_mask;

typedef struct {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, efl;
    uint64_t csgsfs, err, trapno, oldmask, cr2;
    uint64_t fpstate; /* D: user ptr to fxsave blob in frame */
} bfree_sig_mcontext_t;

typedef struct {
    uint64_t pad_uc_flags;
    uint64_t pad_uc_link;
    uint64_t pad_ss_sp;
    uint64_t pad_ss_flags;
    uint64_t pad_ss_size;
    bfree_sig_mcontext_t mc;
    uint64_t uc_sigmask;
} bfree_sig_ucontext_t;

typedef struct {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t si_pad;
    uint64_t si_addr;
    int32_t si_status;
    int32_t si_pad2;
    uint64_t si_value;
    uint8_t pad[96];
} bfree_siginfo_min_t;

typedef struct {
    uint64_t pretcode;
    bfree_sig_ucontext_t uc;
    bfree_siginfo_min_t info;
    uint8_t __attribute__((aligned(16))) fpu[512]; /* D: fxsave area */
} bfree_rt_sigframe_t;

static int bfree_user_range_mapped(uint64_t base, size_t nbytes)
{
    uint64_t a;
    uint64_t end;

    if (nbytes == 0) {
        return 1;
    }
    if (base + (uint64_t)nbytes < base) {
        return 0;
    }
    end = base + (uint64_t)nbytes;
    for (a = base & ~0xfffULL; a < end; a += 0x1000ULL) {
        if (!bfree_user_vaddr_mapped(a)) {
            return 0;
        }
    }
    return 1;
}

static int bfree_sigalt_onstack(uint64_t rsp)
{
    uint64_t sp;
    uint64_t top;

    if (g_sigalt_disable || g_sigalt_sp == 0 || g_sigalt_size == 0) {
        return 0;
    }
    sp = (uint64_t)(uintptr_t)g_sigalt_sp;
    top = sp + (uint64_t)g_sigalt_size;
    return rsp >= sp && rsp < top;
}


static int bfree_sysret_is_magic(long r)
{
    return r == BFREE_SYSRET_EXEC_TRANSFER ||
           r == BFREE_SYSRET_FORK_PARENT ||
           r == BFREE_SYSRET_COOP_SWITCH ||
           r == BFREE_SYSRET_SIGNAL;
}


static void bfree_guest_sig_raise(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (sig != 9 && g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
        return;
    }
    g_guest_sig_pending |= (1ULL << (unsigned)(sig - 1));
    bfree_gthr_wake_sigwait(sig);
}

/* --- signalfd / splice (LTP G) --- */
static bfree_signalfd_t *bfree_find_signalfd(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);
    int i = resolved - BFREE_SIGNALFD_FD_BASE;

    if (i < 0 || i >= BFREE_MAX_SIGNALFD || !g_guest_signalfds[i].used) {
        return 0;
    }
    return &g_guest_signalfds[i];
}

static long sys_linux_signalfd4(long fd, long mask_ptr, long sizemask, long flags)
{
    uint64_t mask = 0;
    int i;
    int pub;
    int cloexec = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    int nonblock = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;

    if (mask_ptr == 0 || sizemask < 8 || !bfree_user_ptr_mapped(mask_ptr)) {
        return -14;
    }
    mask = *(const uint64_t *)(uintptr_t)mask_ptr;
    if (fd >= 0) {
        bfree_signalfd_t *sf = bfree_find_signalfd((int)fd);
        if (!sf) {
            return -9;
        }
        sf->mask = mask;
        return fd;
    }
    for (i = 0; i < BFREE_MAX_SIGNALFD; ++i) {
        if (!g_guest_signalfds[i].used) {
            g_guest_signalfds[i].used = 1;
            g_guest_signalfds[i].magic_fd = BFREE_SIGNALFD_FD_BASE + i;
            g_guest_signalfds[i].mask = mask;
            g_guest_signalfds[i].nonblock = nonblock;
            pub = bfree_guest_fd_publish(g_guest_signalfds[i].magic_fd);
            if (pub < 0) {
                g_guest_signalfds[i].used = 0;
                g_guest_signalfds[i].nonblock = 0;
                return pub;
            }
            if (cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    return -24;
}

static long bfree_linux_read_signalfd(long fd, long buf, long count)
{
    bfree_signalfd_t *sf = bfree_find_signalfd((int)fd);
    uint64_t hit;
    int sig;
    uint8_t *dst;
    size_t i;

    if (!sf) {
        return -9;
    }
    if (buf == 0 || count < 128 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    for (;;) {
        hit = g_guest_sig_pending & sf->mask;
        if (hit != 0) {
            break;
        }
        if (sf->nonblock) {
            return -11; /* EAGAIN */
        }
        {
            int er = bfree_guest_sig_take_eintr();
            if (er < 0) {
                return er;
            }
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
    sig = 0;
    for (i = 0; i < 64; ++i) {
        if ((hit & (1ULL << i)) != 0) {
            sig = (int)i + 1;
            break;
        }
    }
    if (sig <= 0) {
        return -11;
    }
    g_guest_sig_pending &= ~(1ULL << (unsigned)(sig - 1));
    dst = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < 128; ++i) {
        dst[i] = 0;
    }
    /* struct signalfd_siginfo: ssi_signo at offset 0 (uint32) */
    dst[0] = (uint8_t)(sig & 0xff);
    dst[1] = (uint8_t)((sig >> 8) & 0xff);
    dst[2] = (uint8_t)((sig >> 16) & 0xff);
    dst[3] = (uint8_t)((sig >> 24) & 0xff);
    return 128;
}

static long sys_linux_splice(long fd_in, long off_in, long fd_out, long off_out,
                             long len, long flags)
{
    int in_res;
    int out_res;
    bfree_guest_pipe_slot_t *pin = 0;
    bfree_guest_pipe_slot_t *pout = 0;
    bfree_guest_vfile_t *vin = 0;
    bfree_guest_vfile_t *vout = 0;
    bfree_guest_ofd_t *in_ofd = 0;
    bfree_guest_ofd_t *out_ofd = 0;
    size_t n;
    size_t i;
    size_t *ipos;
    size_t *opos;
    size_t room;
    /* SPLICE_F_NONBLOCK is 0x02 on Linux; also accept legacy bit0. */
    int nonblock = ((unsigned long)flags & 3UL) != 0UL;

    if (len < 0) {
        return -22;
    }
    if (len == 0) {
        return 0;
    }
    if (off_in != 0 || off_out != 0) {
        return -22; /* only current-offset splice */
    }
    in_res = bfree_guest_fd_resolve((int)fd_in);
    out_res = bfree_guest_fd_resolve((int)fd_out);
    if (bfree_guest_is_pipe_rd(in_res)) {
        pin = bfree_guest_pipe_slot_from_fd(in_res);
    } else {
        vin = bfree_guest_vfile_from_open_fd(in_res, &in_ofd);
    }
    if (bfree_guest_is_pipe_wr(out_res)) {
        pout = bfree_guest_pipe_slot_from_fd(out_res);
    } else {
        vout = bfree_guest_vfile_from_open_fd(out_res, &out_ofd);
    }
    /* Honor O_NONBLOCK on either end as well as SPLICE_F_NONBLOCK. */
    if (pin && pin->nonblock) {
        nonblock = 1;
    }
    if (pout && pout->nonblock) {
        nonblock = 1;
    }
    if (pin && pout) {
        n = pin->len;
        if (n > (size_t)len) {
            n = (size_t)len;
        }
        room = sizeof(pout->buf) - pout->len;
        if (n > room) {
            n = room;
        }
        if (n == 0) {
            if (pin->len != 0) {
                return -11; /* output pipe full */
            }
            return nonblock ? -11 : 0;
        }
        for (i = 0; i < n; ++i) {
            pout->buf[pout->len + i] = pin->buf[i];
        }
        pout->len += n;
        if (n < pin->len) {
            for (i = 0; i < pin->len - n; ++i) {
                pin->buf[i] = pin->buf[i + n];
            }
        }
        pin->len -= n;
        return (long)n;
    }
    if (vin && !vin->is_dir && pout) {
        ipos = in_ofd ? &in_ofd->pos : &vin->pos;
        if (*ipos >= vin->len) {
            return 0;
        }
        n = vin->len - *ipos;
        if (n > (size_t)len) {
            n = (size_t)len;
        }
        room = sizeof(pout->buf) - pout->len;
        if (n > room) {
            n = room;
        }
        if (n == 0) {
            return -11;
        }
        for (i = 0; i < n; ++i) {
            pout->buf[pout->len + i] = vin->data[*ipos + i];
        }
        pout->len += n;
        *ipos += n;
        return (long)n;
    }
    if (pin && vout && !vout->is_dir && !vout->is_symlink) {
        opos = out_ofd ? &out_ofd->pos : &vout->pos;
        if (*opos > vout->len) {
            *opos = vout->len;
        }
        n = pin->len;
        if (n > (size_t)len) {
            n = (size_t)len;
        }
        if (*opos >= BFREE_GUEST_VFILE_SIZE) {
            return -28;
        }
        if (n > BFREE_GUEST_VFILE_SIZE - *opos) {
            n = BFREE_GUEST_VFILE_SIZE - *opos;
        }
        if (n == 0) {
            if (pin->len != 0) {
                return -11;
            }
            return nonblock ? -11 : 0;
        }
        for (i = 0; i < n; ++i) {
            vout->data[*opos + i] = pin->buf[i];
        }
        *opos += n;
        if (*opos > vout->len) {
            vout->len = *opos;
        }
        if (n < pin->len) {
            for (i = 0; i < pin->len - n; ++i) {
                pin->buf[i] = pin->buf[i + n];
            }
        }
        pin->len -= n;
        return (long)n;
    }
    return -22;
}

/* Linux tee(fdin, fdout, len, flags): copy pipe→pipe without consuming input. */
static long sys_linux_tee(long fd_in, long fd_out, long len, long flags)
{
    int in_res;
    int out_res;
    bfree_guest_pipe_slot_t *pin;
    bfree_guest_pipe_slot_t *pout;
    size_t n;
    size_t i;
    size_t room;
    int nonblock = ((unsigned long)flags & 3UL) != 0UL; /* SPLICE_F_NONBLOCK */

    if (len < 0) {
        return -22;
    }
    if (len == 0) {
        return 0;
    }
    in_res = bfree_guest_fd_resolve((int)fd_in);
    out_res = bfree_guest_fd_resolve((int)fd_out);
    if (!bfree_guest_is_pipe_rd(in_res) || !bfree_guest_is_pipe_wr(out_res)) {
        return -22;
    }
    pin = bfree_guest_pipe_slot_from_fd(in_res);
    pout = bfree_guest_pipe_slot_from_fd(out_res);
    if (!pin || !pout) {
        return -9;
    }
    if (pin == pout) {
        return -22;
    }
    if (pin->nonblock || pout->nonblock) {
        nonblock = 1;
    }
    n = pin->len;
    if (n > (size_t)len) {
        n = (size_t)len;
    }
    room = sizeof(pout->buf) - pout->len;
    if (n > room) {
        n = room;
    }
    if (n == 0) {
        if (pin->len != 0) {
            return -11; /* output pipe full */
        }
        return nonblock ? -11 : 0;
    }
    for (i = 0; i < n; ++i) {
        pout->buf[pout->len + i] = pin->buf[i];
    }
    pout->len += n;
    return (long)n;
}

/* Linux vmsplice(fd, iov, nr_segs, flags): copy user iovecs into a pipe write end. */
static long sys_linux_vmsplice(long fd, long iov_ptr, long nr_segs, long flags)
{
    typedef struct {
        uint64_t iov_base;
        uint64_t iov_len;
    } bfree_iovec64_t;
    int out_res;
    bfree_guest_pipe_slot_t *pout;
    long total = 0;
    long i;
    int nonblock = ((unsigned long)flags & 3UL) != 0UL; /* SPLICE_F_NONBLOCK */

    if (nr_segs < 0 || nr_segs > 1024) {
        return -22;
    }
    if (nr_segs == 0) {
        return 0;
    }
    if (iov_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)iov_ptr,
                               (uint64_t)nr_segs * sizeof(bfree_iovec64_t))) {
        return -14;
    }
    out_res = bfree_guest_fd_resolve((int)fd);
    if (!bfree_guest_is_pipe_wr(out_res)) {
        return -22;
    }
    pout = bfree_guest_pipe_slot_from_fd(out_res);
    if (!pout) {
        return -9;
    }
    if (pout->nonblock) {
        nonblock = 1;
    }
    for (i = 0; i < nr_segs; ++i) {
        bfree_iovec64_t iov;
        const uint8_t *src;
        size_t n;
        size_t room;
        size_t j;
        const uint8_t *raw = (const uint8_t *)(uintptr_t)iov_ptr;

        for (j = 0; j < sizeof(iov); ++j) {
            ((uint8_t *)&iov)[j] = raw[(size_t)i * sizeof(iov) + j];
        }
        if (iov.iov_len == 0) {
            continue;
        }
        if (iov.iov_base == 0 ||
            !bfree_user_buf_mapped(iov.iov_base, iov.iov_len)) {
            return total > 0 ? total : -14;
        }
        room = sizeof(pout->buf) - pout->len;
        if (room == 0) {
            /* Full pipe: NONBLOCK / O_NONBLOCK → EAGAIN; else soft-0. */
            return total > 0 ? total : (nonblock ? -11L : 0L);
        }
        n = (size_t)iov.iov_len;
        if (n > room) {
            n = room;
        }
        src = (const uint8_t *)(uintptr_t)iov.iov_base;
        for (j = 0; j < n; ++j) {
            pout->buf[pout->len + j] = src[j];
        }
        pout->len += n;
        total += (long)n;
        if (n < (size_t)iov.iov_len) {
            break; /* pipe full */
        }
    }
    return total;
}

static int bfree_guest_sig_is_blocked(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return 0;
    }
    return (g_guest_sig_mask & (1ULL << (unsigned)(sig - 1))) != 0ULL;
}

static void bfree_guest_sig_arm_catch(int sig)
{
    if (sig <= 0 || sig >= BFREE_NSIG) {
        return;
    }
    if (g_sig_in_handler || g_sig_deliver_sig != 0) {
        /* D: queue one nested CATCH for delivery after rt_sigreturn */
        if (g_sig_nested_pending == 0 && g_guest_sig_disp[sig] == BFREE_SIG_CATCH) {
            g_sig_nested_pending = sig;
        }
        return;
    }
    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH) {
        return;
    }
    g_sig_deliver_sig = sig;
}

static void bfree_guest_sig_arm_pending_catch(void)
{
    const int candidates[] = { 2, 14, 13, 17, 15, 1, 10 };
    int i;

    if (g_sig_in_handler || g_sig_deliver_sig != 0) {
        return;
    }
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        int sig = candidates[i];
        uint64_t bit = 1ULL << (unsigned)(sig - 1);
        if ((g_guest_sig_pending & bit) == 0ULL) {
            continue;
        }
        if (bfree_guest_sig_is_blocked(sig)) {
            continue;
        }
        if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH) {
            continue;
        }
        g_guest_sig_pending &= ~bit;
        g_sig_deliver_sig = sig;
        return;
    }
}

static int bfree_guest_sig_take_eintr(void)
{
    const int candidates[] = { 2, 14, 13, 17, 15, 1, 10 };
    int i;
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        int sig = candidates[i];
        uint64_t bit = 1ULL << (unsigned)(sig - 1);
        if ((g_guest_sig_pending & bit) == 0ULL) {
            continue;
        }
        if (bfree_guest_sig_is_blocked(sig)) {
            continue;
        }
        if (sig != 9 && g_guest_sig_disp[sig] == BFREE_SIG_IGN) {
            g_guest_sig_pending &= ~bit;
            continue;
        }
        g_guest_sig_pending &= ~bit;
        if (g_guest_sig_disp[sig] == BFREE_SIG_CATCH) {
            bfree_guest_sig_arm_catch(sig);
        }
        return -4; /* EINTR */
    }
    return 0;
}

static long bfree_guest_sig_try_deliver(long syscall_ret)
{
    int sig;
    void *handler;
    void *restorer;
    uint64_t rsp;
    uint64_t frame_base;
    bfree_rt_sigframe_t *frame;

    if (bfree_sysret_is_magic(syscall_ret)) {
        return syscall_ret;
    }
    bfree_guest_sig_arm_pending_catch();
    if (g_sig_in_handler || g_sig_deliver_sig == 0) {
        return syscall_ret;
    }

    sig = g_sig_deliver_sig;
    g_sig_deliver_sig = 0;
    handler = g_guest_sig_handler[sig];
    restorer = g_guest_sig_restorer[sig];

    if (g_guest_sig_disp[sig] != BFREE_SIG_CATCH ||
        handler == 0 || handler == (void *)(uintptr_t)1 ||
        restorer == 0 ||
        !bfree_user_ptr_mapped((long)(uintptr_t)handler) ||
        !bfree_user_ptr_mapped((long)(uintptr_t)restorer)) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }

    rsp = g_bfree_user_sysret_rsp;
    /* H17: SA_ONSTACK → grow down from registered altstack top if not already on it. */
    if ((g_guest_sig_flags[sig] & BFREE_SA_ONSTACK) != 0UL &&
        !g_sigalt_disable && g_sigalt_sp != 0 && g_sigalt_size >= (size_t)BFREE_MINSIGSTKSZ &&
        !bfree_sigalt_onstack(rsp)) {
        rsp = ((uint64_t)(uintptr_t)g_sigalt_sp + (uint64_t)g_sigalt_size) & ~15ULL;
    }
    if (rsp < (uint64_t)sizeof(bfree_rt_sigframe_t) + 16ULL) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    rsp &= ~15ULL;
    frame_base = rsp - (uint64_t)sizeof(bfree_rt_sigframe_t);
    if (!bfree_user_range_mapped(frame_base, sizeof(bfree_rt_sigframe_t))) {
        bfree_guest_sig_raise(sig);
        return syscall_ret;
    }
    frame = (bfree_rt_sigframe_t *)(uintptr_t)frame_base;
    memset(frame, 0, sizeof(*frame));
    frame->pretcode = (uint64_t)(uintptr_t)restorer;
    frame->info.si_signo = sig;
    frame->info.si_code = 0;

    g_sig_saved_rax = (uint64_t)(int64_t)syscall_ret;
    g_sig_saved_rdi = 0;
    g_sig_saved_rsi = 0;
    g_sig_saved_rdx = g_bfree_user_sysret_rdx;
    g_sig_saved_rbx = g_bfree_user_sysret_rbx;
    g_sig_saved_rbp = g_bfree_user_sysret_rbp;
    g_sig_saved_r12 = g_bfree_user_sysret_r12;
    g_sig_saved_r13 = g_bfree_user_sysret_r13;
    g_sig_saved_r14 = g_bfree_user_sysret_r14;
    g_sig_saved_r15 = g_bfree_user_sysret_r15;
    g_sig_saved_rip = g_bfree_user_sysret_rcx;
    g_sig_saved_rsp = g_bfree_user_sysret_rsp;
    g_sig_saved_rflags = g_bfree_user_sysret_r11;
    g_sig_saved_mask = g_guest_sig_mask;

    frame->uc.mc.r12 = g_sig_saved_r12;
    frame->uc.mc.r13 = g_sig_saved_r13;
    frame->uc.mc.r14 = g_sig_saved_r14;
    frame->uc.mc.r15 = g_sig_saved_r15;
    frame->uc.mc.rdi = g_sig_saved_rdi;
    frame->uc.mc.rsi = g_sig_saved_rsi;
    frame->uc.mc.rbp = g_sig_saved_rbp;
    frame->uc.mc.rbx = g_sig_saved_rbx;
    frame->uc.mc.rdx = g_sig_saved_rdx;
    frame->uc.mc.rax = g_sig_saved_rax;
    frame->uc.mc.rsp = g_sig_saved_rsp;
    frame->uc.mc.rip = g_sig_saved_rip;
    frame->uc.mc.efl = g_sig_saved_rflags;
    frame->uc.mc.oldmask = g_sig_saved_mask;
    frame->uc.uc_sigmask = g_sig_saved_mask;
    frame->uc.mc.fpstate = frame_base + BFREE_OFFSETOF(bfree_rt_sigframe_t, fpu);
    memset(frame->fpu, 0, sizeof(frame->fpu));
    __asm__ volatile("fxsave %0" : "=m"(frame->fpu) : : "memory");

    g_guest_sig_mask |= g_guest_sig_sa_mask[sig];
    if ((g_guest_sig_flags[sig] & BFREE_SA_NODEFER) == 0UL) {
        g_guest_sig_mask |= (1ULL << (unsigned)(sig - 1));
    }
    g_guest_sig_mask &= ~((1ULL << 8) | (1ULL << 18));
    g_sig_mask_pushed = 1;
    g_sig_in_handler = 1;

    g_bfree_sysret_exec_rsp = frame_base;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)handler;
    g_bfree_sysret_exec_r11 = g_bfree_user_sysret_r11 | 0x200ULL;
    g_bfree_sysret_exec_rdi = (uint64_t)(unsigned)sig;
    if ((g_guest_sig_flags[sig] & BFREE_SA_SIGINFO) != 0UL) {
        g_bfree_sysret_exec_rsi = frame_base + BFREE_OFFSETOF(bfree_rt_sigframe_t, info);
        g_bfree_sysret_exec_rdx = frame_base + BFREE_OFFSETOF(bfree_rt_sigframe_t, uc);
    } else {
        g_bfree_sysret_exec_rsi = 0;
        g_bfree_sysret_exec_rdx = 0;
    }
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = 0;
    g_bfree_sig_saved_rbx = g_sig_saved_rbx;
    g_bfree_sig_saved_rbp = g_sig_saved_rbp;
    g_bfree_sig_saved_r12 = g_sig_saved_r12;
    g_bfree_sig_saved_r13 = g_sig_saved_r13;
    g_bfree_sig_saved_r14 = g_sig_saved_r14;
    g_bfree_sig_saved_r15 = g_sig_saved_r15;
    g_bfree_sig_saved_rdx = g_sig_saved_rdx;
    return BFREE_SYSRET_SIGNAL;
}

static long sys_linux_rt_sigreturn(void)
{
    if (!g_sig_in_handler) {
        return -22;
    }
    g_sig_in_handler = 0;
    if (g_sig_mask_pushed) {
        g_guest_sig_mask = g_sig_saved_mask;
        g_sig_mask_pushed = 0;
    }
    if (g_sig_nested_pending != 0) {
        int ns = g_sig_nested_pending;
        g_sig_nested_pending = 0;
        bfree_guest_sig_arm_catch(ns);
    }
    g_bfree_sysret_exec_rsp = g_sig_saved_rsp;
    g_bfree_sysret_exec_rcx = g_sig_saved_rip;
    g_bfree_sysret_exec_r11 = g_sig_saved_rflags | 0x200ULL;
    g_bfree_sysret_exec_rdi = g_sig_saved_rdi;
    g_bfree_sysret_exec_rsi = g_sig_saved_rsi;
    g_bfree_sysret_exec_rdx = g_sig_saved_rdx;
    g_bfree_sysret_exec_cr3 = 0;
    g_bfree_sysret_sig_rax = g_sig_saved_rax;
    g_bfree_sig_saved_rbx = g_sig_saved_rbx;
    g_bfree_sig_saved_rbp = g_sig_saved_rbp;
    g_bfree_sig_saved_r12 = g_sig_saved_r12;
    g_bfree_sig_saved_r13 = g_sig_saved_r13;
    g_bfree_sig_saved_r14 = g_sig_saved_r14;
    g_bfree_sig_saved_r15 = g_sig_saved_r15;
    g_bfree_sig_saved_rdx = g_sig_saved_rdx;
    return BFREE_SYSRET_SIGNAL;
}

#endif /* BFREE_H01_SIGFRAME_WIRED */

#ifndef BFREE_RESTORE_SOFT_BODIES
#define BFREE_RESTORE_SOFT_BODIES 1
static long bfree_guest_exit_from_fork_signal(int sig)
{
    int as_copy = g_guest_fork_was_as_copy;
    int st = sig & 0x7f;
    page_table_t *resume_pt = bfree_process_parent_pt();

    if (!resume_pt && knl_current_task) {
        resume_pt = (page_table_t *)knl_current_task->page_table_base;
    }
    if (st == 0) {
        st = 9;
    }
    bfree_process_exit_child_signal(st);
    g_guest_fork_active = 0;
    g_guest_fork_status = st;
    g_guest_fork_status_ready = 1;
    bfree_guest_sig_raise(17);
    bfree_coop_fd_switch_to(0);
    g_coop_child_blocked = 0;
    bfree_guest_stdio_heal_pipes();
    if (!as_copy) {
        size_t i;
        for (i = 0; i < sizeof(g_guest_cwd); ++i) {
            g_guest_cwd[i] = g_guest_fork_saved_cwd[i];
            if (g_guest_fork_saved_cwd[i] == '\0') {
                break;
            }
        }
        g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\0';
        g_guest_heap_next = g_guest_fork_saved_heap_next;
        g_guest_brk = g_guest_fork_saved_brk;
        bfree_guest_vfork_stack_restore();
    } else {
        bfree_guest_restore_parent_isol(1);
        bfree_coop_as_switch_to(0);
    }
    g_guest_fork_was_as_copy = 0;
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = g_guest_fork_saved_fsbase;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, g_guest_fork_saved_fsbase);
    g_bfree_sysret_exec_cr3 = 0;
    {
        int parent_waiting =
            (g_coop_parent_in_wait || g_guest_wait_status_ptr != 0 ||
             g_guest_waitid_active) ? 1 : 0;
        int wst = 0;
        long wr = 0;

        if (parent_waiting) {
            wr = bfree_process_wait4(
                g_guest_fork_pid > 0 ? (long)g_guest_fork_pid : -1L, &wst, 0);
            if (g_guest_wait_status_ptr != 0 &&
                bfree_user_ptr_mapped(g_guest_wait_status_ptr)) {
                *(int *)(uintptr_t)g_guest_wait_status_ptr =
                    (wr > 0) ? wst : (st & 0x7f);
            }
            if (g_guest_waitid_active && g_guest_waitid_infop != 0 &&
                bfree_user_ptr_mapped(g_guest_waitid_infop)) {
                uint8_t *raw = (uint8_t *)(uintptr_t)g_guest_waitid_infop;
                size_t i;
                int use_st = (wr > 0) ? wst : (((int)(st & 0xff)) << 8);
                long use_pid = (wr > 0) ? wr : (long)g_guest_fork_pid;
                for (i = 0; i < 32; ++i) {
                    raw[i] = 0;
                }
                *(int *)(void *)(raw + 0) = BFREE_SIGCHLD;
                *(int *)(void *)(raw + 8) = BFREE_CLD_EXITED;
                *(int *)(void *)(raw + 16) = (int)use_pid;
                *(int *)(void *)(raw + 24) = (use_st >> 8) & 0xff;
            }
        }
        if (as_copy && g_coop_parent_started) {
            bfree_coop_publish_parent_resume();
            if (parent_waiting) {
                g_coop_parent_resume_rax =
                    g_guest_waitid_active
                        ? 0ULL
                        : (uint64_t)(wr > 0 ? wr : (long)g_guest_fork_pid);
                g_coop_parent_resume_mode = 2;
            }
            bfree_coop_arm_parent_resume();
        } else {
            g_bfree_fork_parent_ret =
                g_guest_waitid_active
                    ? 0ULL
                    : (parent_waiting && wr > 0
                           ? (uint64_t)wr
                           : (uint64_t)(long)g_guest_fork_pid);
        }
    }
    g_coop_parent_started = 0;
    g_coop_parent_in_wait = 0;
    g_guest_wait_status_ptr = 0;
    g_guest_waitid_active = 0;
    g_guest_waitid_infop = 0;
    if (resume_pt && knl_current_task) {
        knl_current_task->page_table_base = resume_pt;
        __asm__ volatile("mov %0, %%cr3" :: "r"(resume_pt) : "memory");
    }
    g_bfree_sysret_exec_cr3 = 0;
    g_guest_sig_pending &= ~(1ULL << 16); /* SIGCHLD */
    uart_puts("[VFORK] signal exit sig=");
    uart_puthex64((uint64_t)(unsigned)st);
    uart_puts("\n");
    return BFREE_SYSRET_FORK_PARENT;
}
#endif
/* === end restore soft bodies === */


/* ==== P8 restore: pread/select + sockets + flock/alarm ==== */
static uint32_t bfree_inet_match_addr(uint32_t addr)
{
    if (addr == BFREE_INADDR_ANY) {
        return BFREE_INADDR_LOOPBACK;
    }
    if ((addr & 0xFF000000u) == 0x7F000000u) {
        return BFREE_INADDR_LOOPBACK;
    }
    if ((addr & 0xFFFFFF00u) == 0x0A000200u) {
        return BFREE_INADDR_LOOPBACK;
    }
    return addr;
}

static int bfree_inet_listener_matches(uint32_t bound_addr, uint32_t peer_addr)
{
    uint32_t b = bfree_inet_match_addr(bound_addr);
    uint32_t p = bfree_inet_match_addr(peer_addr);

    if (bound_addr == BFREE_INADDR_ANY) {
        return 1;
    }
    return b == p;
}

static int bfree_inet_parse_sockaddr(long addr, long addrlen, uint32_t *out_addr, uint16_t *out_port)
{
    const uint8_t *raw;
    uint16_t family;
    uint16_t port_be;
    uint32_t addr_be;

    if (addrlen < 8 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    family = (uint16_t)(raw[0] | (raw[1] << 8));
    if (family == (uint16_t)BFREE_LINUX_AF_INET6) {
        /* sockaddr_in6: family(2) port(2) flowinfo(4) addr(16) scope_id(4). */
        const uint8_t *v6 = raw + 8;
        size_t k;
        int lead12_zero = 1;
        int lead10_zero = 1;

        if (addrlen < 24) {
            return -22;
        }
        if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)addr, 24)) {
            return -14;
        }
        port_be = (uint16_t)(raw[2] | (raw[3] << 8));
        if (out_port) {
            *out_port = bfree_inet_ntohs(port_be);
        }
        for (k = 0; k < 12U; ++k) {
            if (v6[k] != 0) {
                lead12_zero = 0;
                if (k < 10U) {
                    lead10_zero = 0;
                }
                break;
            }
        }
        if (out_addr) {
            if (lead12_zero && v6[12] == 0 && v6[13] == 0 && v6[14] == 0) {
                /* :: → INADDR_ANY, ::1 → IPv4 loopback. */
                *out_addr = (v6[15] == 0) ? BFREE_INADDR_ANY
                          : (v6[15] == 1) ? BFREE_INADDR_LOOPBACK
                                          : 0xFFFFFFFEU;
            } else if (lead10_zero && v6[10] == 0xff && v6[11] == 0xff) {
                /* ::ffff:a.b.c.d */
                *out_addr = ((uint32_t)v6[12] << 24) | ((uint32_t)v6[13] << 16) |
                            ((uint32_t)v6[14] << 8) | (uint32_t)v6[15];
            } else {
                *out_addr = 0xFFFFFFFEU; /* never routable → ENETUNREACH */
            }
        }
        return 0;
    }
    if (family != (uint16_t)BFREE_LINUX_AF_INET) {
        return -97; /* EAFNOSUPPORT */
    }
    port_be = (uint16_t)(raw[2] | (raw[3] << 8));
    addr_be = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) |
              ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    if (out_port) {
        *out_port = bfree_inet_ntohs(port_be);
    }
    if (out_addr) {
        *out_addr = bfree_inet_ntohl(addr_be);
    }
    return 0;
}
static int bfree_inet_is_guest_routable(uint32_t addr)
{
    if (addr == BFREE_INADDR_ANY) {
        return 1;
    }
    if ((addr & 0xFF000000u) == 0x7F000000u) {
        return 1; /* 127.0.0.0/8 */
    }
    if ((addr & 0xFFFFFF00u) == 0x0A000200u) {
        return 1; /* 10.0.2.0/24 — QEMU user/slirp guest LAN */
    }
    return 0;
}
static int bfree_inet_is_loopback(uint32_t addr)
{
    return bfree_inet_is_guest_routable(addr);
}
/* Listener backlog helpers shared by the AF_INET and AF_UNIX paths. */
static void bfree_sock_accept_q_reset(int *q_rd, int *q_wr, int *q_len,
                                      int *backlog)
{
    int i;

    for (i = 0; i < BFREE_ACCEPT_Q; ++i) {
        q_rd[i] = -1;
        q_wr[i] = -1;
    }
    *q_len = 0;
    *backlog = 1;
}

static int bfree_sock_accept_q_push(int *q_rd, int *q_wr, int *q_len,
                                    int backlog, int rd, int wr)
{
    int cap = (backlog < 1) ? 1 : backlog;

    if (cap > BFREE_ACCEPT_Q) {
        cap = BFREE_ACCEPT_Q;
    }
    if (*q_len >= cap) {
        return -11; /* EAGAIN — backlog full */
    }
    q_rd[*q_len] = rd;
    q_wr[*q_len] = wr;
    ++(*q_len);
    return 0;
}

static int bfree_sock_accept_q_pop(int *q_rd, int *q_wr, int *q_len,
                                   int *rd_out, int *wr_out)
{
    int i;

    if (*q_len <= 0) {
        return -11;
    }
    *rd_out = q_rd[0];
    *wr_out = q_wr[0];
    for (i = 1; i < *q_len; ++i) {
        q_rd[i - 1] = q_rd[i];
        q_wr[i - 1] = q_wr[i];
    }
    --(*q_len);
    q_rd[*q_len] = -1;
    q_wr[*q_len] = -1;
    return 0;
}

static long sys_linux_socket(long domain, long type, long protocol)
{
    int i;
    int stype = (int)(type & 0xFF);
    int cloexec = ((unsigned long)type & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    int nonblock = ((unsigned long)type & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;
    int pub;

    if (domain == BFREE_LINUX_AF_INET || domain == BFREE_LINUX_AF_INET6) {
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (!g_inet_socks[i].used) {
                g_inet_socks[i].used = 1;
                g_inet_socks[i].listening = 0;
                g_inet_socks[i].connected = 0;
                g_inet_socks[i].bound = 0;
                g_inet_socks[i].is_dgram = (stype == 2); /* SOCK_DGRAM */
                g_inet_socks[i].is_v6 = (domain == BFREE_LINUX_AF_INET6);
                g_inet_socks[i].is_raw = (stype == BFREE_SOCK_RAW);
                g_inet_socks[i].ip_proto = (int)protocol;
                g_inet_socks[i].shut_rd = 0;
                g_inet_socks[i].shut_wr = 0;
                g_inet_socks[i].nonblock = nonblock;
                g_inet_socks[i].so_reuseaddr = 0;
                g_inet_socks[i].so_reuseport = 0;
                g_inet_socks[i].so_keepalive = 0;
                g_inet_socks[i].so_broadcast = 0;
                g_inet_socks[i].so_linger_on = 0;
                g_inet_socks[i].so_linger_sec = 0;
                g_inet_socks[i].so_oobinline = 0;
                g_inet_socks[i].so_error = 0;
                g_inet_socks[i].tcp_nodelay = 0;
                g_inet_socks[i].ip_ttl = 64;
                g_inet_socks[i].ip_tos = 0;
                g_inet_socks[i].so_rcvbuf = 8192;
                g_inet_socks[i].so_sndbuf = 8192;
                g_inet_socks[i].so_rcvtimeo_us = 0;
                g_inet_socks[i].so_sndtimeo_us = 0;
                g_inet_socks[i].addr = BFREE_INADDR_ANY;
                g_inet_socks[i].port = 0;
                g_inet_socks[i].accept_rd = -1;
                g_inet_socks[i].accept_wr = -1;
                g_inet_socks[i].pipe_magic = -1;
                g_inet_socks[i].tcp_pcb = -1;
                g_inet_socks[i].peer_addr = 0;
                g_inet_socks[i].peer_port = 0;
                g_inet_socks[i].dg_head = 0;
                g_inet_socks[i].dg_count = 0;
                bfree_sock_accept_q_reset(g_inet_socks[i].q_rd,
                                          g_inet_socks[i].q_wr,
                                          &g_inet_socks[i].q_len,
                                          &g_inet_socks[i].listen_backlog);
                pub = bfree_guest_fd_publish((int)BFREE_INET_FD_BASE + i);
                if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                    g_guest_fd_cloexec[pub] = 1;
                }
                return pub;
            }
        }
        return -24;
    }
    if (domain != BFREE_LINUX_AF_UNIX) {
        return -97;
    }
    for (i = 0; i < BFREE_UNIX_SLOTS; ++i) {
        if (!g_unix_socks[i].used) {
            g_unix_socks[i].used = 1;
            g_unix_socks[i].listening = 0;
            g_unix_socks[i].connected = 0;
            g_unix_socks[i].is_dgram = (stype == BFREE_SOCK_DGRAM);
            g_unix_socks[i].shut_rd = 0;
            g_unix_socks[i].shut_wr = 0;
            g_unix_socks[i].nonblock = nonblock;
            g_unix_socks[i].so_keepalive = 0;
            g_unix_socks[i].so_rcvbuf = 8192;
            g_unix_socks[i].so_sndbuf = 8192;
            g_unix_socks[i].accept_rd = -1;
            g_unix_socks[i].accept_wr = -1;
            g_unix_socks[i].pipe_magic = -1;
            g_unix_socks[i].path[0] = '\0';
            g_unix_socks[i].peer_path[0] = '\0';
            g_unix_socks[i].dg_src[0] = '\0';
            g_unix_socks[i].dg_pending = 0;
            g_unix_socks[i].dg_len = 0;
            bfree_sock_accept_q_reset(g_unix_socks[i].q_rd,
                                      g_unix_socks[i].q_wr,
                                      &g_unix_socks[i].q_len,
                                      &g_unix_socks[i].listen_backlog);
            pub = bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + i);
            if (pub >= 0 && cloexec && pub < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[pub] = 1;
            }
            return pub;
        }
    }
    return -24;
}

static int bfree_inet_is_loopback(uint32_t addr);
static void bfree_inet_udp_bind_stack(uint16_t port);
static void bfree_inet_udp_unbind_stack(uint16_t port);

static long sys_linux_bind(long sockfd, long addr, long addrlen)
{
    int idx, i;
    const uint8_t *raw;
    size_t n;
    uint32_t in_addr;
    uint16_t in_port;
    long perr;

    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        perr = bfree_inet_parse_sockaddr(addr, addrlen, &in_addr, &in_port);
        if (perr != 0) {
            return perr;
        }
        if (!bfree_inet_is_loopback(in_addr)) {
            return -101; /* ENETUNREACH — only loopback without NIC */
        }
        if (in_port == 0) {
            /* Ephemeral: pick unused 40000+idx */
            in_port = (uint16_t)(40000 + idx);
        }
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (i != idx && g_inet_socks[i].used && g_inet_socks[i].bound &&
                g_inet_socks[i].port == in_port &&
                (g_inet_socks[i].addr == in_addr ||
                 g_inet_socks[i].addr == BFREE_INADDR_ANY ||
                 in_addr == BFREE_INADDR_ANY)) {
                /* SO_REUSEADDR: allow when both sides opted in. */
                if (!(g_inet_socks[idx].so_reuseaddr && g_inet_socks[i].so_reuseaddr)) {
                    return -98; /* EADDRINUSE */
                }
            }
        }
        g_inet_socks[idx].addr = in_addr;
        g_inet_socks[idx].port = in_port;
        g_inet_socks[idx].bound = 1;
        if (g_inet_socks[idx].is_dgram) {
            bfree_inet_udp_bind_stack(in_port);
        }
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (addrlen < 4 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    n = 0;
    while (n + 2U < (size_t)addrlen && n + 1U < sizeof(g_unix_socks[idx].path) &&
           raw[2 + n] != 0) {
        g_unix_socks[idx].path[n] = (char)raw[2 + n];
        ++n;
    }
    g_unix_socks[idx].path[n] = '\0';
    if (n == 0) {
        return -22;
    }
    for (i = 0; i < BFREE_UNIX_SLOTS; ++i) {
        if (i != idx && g_unix_socks[i].used && g_unix_socks[i].path[0] &&
            strcmp(g_unix_socks[i].path, g_unix_socks[idx].path) == 0) {
            return -98;
        }
    }
    return 0;
}

static int bfree_sock_clamp_backlog(long backlog)
{
    if (backlog < 1) {
        return 1;
    }
    if (backlog > BFREE_ACCEPT_Q) {
        return BFREE_ACCEPT_Q;
    }
    return (int)backlog;
}

static long sys_linux_listen(long sockfd, long backlog)
{
    int idx;

    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (g_inet_socks[idx].is_dgram || g_inet_socks[idx].is_raw) {
            return -95; /* EOPNOTSUPP */
        }
        if (!g_inet_socks[idx].bound) {
            return -22;
        }
        g_inet_socks[idx].listening = 1;
        g_inet_socks[idx].listen_backlog = bfree_sock_clamp_backlog(backlog);
        g_inet_socks[idx].q_len = 0;
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (g_unix_socks[idx].is_dgram) {
        return -95; /* EOPNOTSUPP */
    }
    if (g_unix_socks[idx].path[0] == '\0') {
        return -22;
    }
    g_unix_socks[idx].listening = 1;
    g_unix_socks[idx].listen_backlog = bfree_sock_clamp_backlog(backlog);
    g_unix_socks[idx].q_len = 0;
    return 0;
}

static long sys_linux_connect(long sockfd, long addr, long addrlen)
{
    int idx, li, i;
    const uint8_t *raw;
    char path[96];
    size_t n;
    uint32_t in_addr;
    uint16_t in_port;
    long perr;

    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        perr = bfree_inet_parse_sockaddr(addr, addrlen, &in_addr, &in_port);
        if (perr != 0) {
            return perr;
        }
        if (!bfree_inet_is_loopback(in_addr)) {
            return -101; /* ENETUNREACH — no NIC / non-loopback */
        }
        if (g_inet_socks[idx].is_dgram) {
            /* UDP connect: remember default peer only; no handshake. */
            g_inet_socks[idx].peer_addr = in_addr;
            g_inet_socks[idx].peer_port = in_port;
            g_inet_socks[idx].connected = 1;
            return 0;
        }
        for (li = 0; li < BFREE_INET_SLOTS; ++li) {
            if (g_inet_socks[li].used && g_inet_socks[li].listening &&
                g_inet_socks[li].port == in_port &&
                bfree_inet_listener_matches(g_inet_socks[li].addr, in_addr)) {
                break;
            }
        }
        if (li >= BFREE_INET_SLOTS) {
            /* F2: outbound TCP via e1000/slirp (10.0.2/24) when no local listener. */
            if ((in_addr & 0xFFFFFF00u) == 0x0A000200u) {
                uint16_t sport = g_inet_socks[idx].bound
                                     ? g_inet_socks[idx].port
                                     : 0;
                int pcb = tcp_min_connect(bfree_inet_ntohl(in_addr), in_port, sport);
                if (pcb < 0) {
                    return (long)pcb;
                }
                g_inet_socks[idx].tcp_pcb = pcb;
                g_inet_socks[idx].pipe_magic = -1;
                g_inet_socks[idx].peer_addr = in_addr;
                g_inet_socks[idx].peer_port = in_port;
                g_inet_socks[idx].addr = in_addr;
                g_inet_socks[idx].port = in_port;
                /* Optimistic success: BusyBox xconnect dies on EINPROGRESS.
                 * Handshake finishes on first write/read via tcp_min_pump. */
                g_inet_socks[idx].connected = 1;
                return 0;
            }
            g_inet_socks[idx].so_error = 111;
            return -111; /* ECONNREFUSED — no loopback listener */
        }
        if (g_inet_socks[li].q_len >= g_inet_socks[li].listen_backlog ||
            g_inet_socks[li].q_len >= BFREE_ACCEPT_Q) {
            return -11; /* EAGAIN — backlog full */
        }
        {
            int slot0 = -1;
            int slot1 = -1;
            int rd0, wr0, rd1, wr1;

            for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
                if (!g_guest_pipes[i].used) {
                    if (slot0 < 0) {
                        slot0 = i;
                    } else {
                        slot1 = i;
                        break;
                    }
                }
            }
            if (slot0 < 0 || slot1 < 0) {
                return -24;
            }
            g_guest_pipes[slot0].used = 1;
            g_guest_pipes[slot0].rd_open = 1;
            g_guest_pipes[slot0].wr_open = 1;
            g_guest_pipes[slot0].len = 0;
            g_guest_pipes[slot0].nonblock = g_inet_socks[idx].nonblock;
            g_guest_pipes[slot1].used = 1;
            g_guest_pipes[slot1].rd_open = 1;
            g_guest_pipes[slot1].wr_open = 1;
            g_guest_pipes[slot1].len = 0;
            g_guest_pipes[slot1].nonblock = g_inet_socks[idx].nonblock;
            rd0 = bfree_guest_pipe_magic_fd(slot0, 0);
            wr0 = bfree_guest_pipe_magic_fd(slot0, 1);
            rd1 = bfree_guest_pipe_magic_fd(slot1, 0);
            wr1 = bfree_guest_pipe_magic_fd(slot1, 1);
            /* Client writes wr0 → accepted reads rd0; accepted writes wr1 → client reads rd1. */
            if (bfree_sock_accept_q_push(g_inet_socks[li].q_rd,
                                         g_inet_socks[li].q_wr,
                                         &g_inet_socks[li].q_len,
                                         g_inet_socks[li].listen_backlog,
                                         rd0, wr1) != 0) {
                g_guest_pipes[slot0].used = 0;
                g_guest_pipes[slot1].used = 0;
                return -11;
            }
            g_inet_socks[idx].connected = 1;
            g_inet_socks[idx].pipe_magic = wr0;
            g_inet_socks[idx].accept_rd = rd1;
            g_inet_socks[idx].accept_wr = -1;
            g_inet_socks[idx].addr = in_addr;
            g_inet_socks[idx].port = in_port;
            g_inet_socks[idx].peer_addr = in_addr;
            g_inet_socks[idx].peer_port = in_port;
            return 0;
        }
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (addrlen < 4 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    n = 0;
    while (n + 2U < (size_t)addrlen && n + 1U < sizeof(path) && raw[2 + n] != 0) {
        path[n] = (char)raw[2 + n];
        ++n;
    }
    path[n] = '\0';
    if (g_unix_socks[idx].is_dgram) {
        /* Datagram connect(): remember the default destination path only. */
        for (li = 0; li < BFREE_UNIX_SLOTS; ++li) {
            if (g_unix_socks[li].used && g_unix_socks[li].is_dgram &&
                g_unix_socks[li].path[0] != '\0' &&
                strcmp(g_unix_socks[li].path, path) == 0) {
                break;
            }
        }
        if (li >= BFREE_UNIX_SLOTS) {
            return -111; /* ECONNREFUSED — nothing bound to that path */
        }
        for (n = 0; path[n] != '\0' &&
                    n + 1U < sizeof(g_unix_socks[idx].peer_path); ++n) {
            g_unix_socks[idx].peer_path[n] = path[n];
        }
        g_unix_socks[idx].peer_path[n] = '\0';
        g_unix_socks[idx].connected = 1;
        return 0;
    }
    for (li = 0; li < BFREE_UNIX_SLOTS; ++li) {
        if (g_unix_socks[li].used && g_unix_socks[li].listening &&
            strcmp(g_unix_socks[li].path, path) == 0) {
            break;
        }
    }
    if (li >= BFREE_UNIX_SLOTS) {
        return -111;
    }
    if (g_unix_socks[li].q_len >= g_unix_socks[li].listen_backlog ||
        g_unix_socks[li].q_len >= BFREE_ACCEPT_Q) {
        return -11;
    }
    {
        int slot0 = -1;
        int slot1 = -1;
        int rd0, wr0, rd1, wr1;

        for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
            if (!g_guest_pipes[i].used) {
                if (slot0 < 0) {
                    slot0 = i;
                } else {
                    slot1 = i;
                    break;
                }
            }
        }
        if (slot0 < 0 || slot1 < 0) {
            return -24;
        }
        g_guest_pipes[slot0].used = 1;
        g_guest_pipes[slot0].rd_open = 1;
        g_guest_pipes[slot0].wr_open = 1;
        g_guest_pipes[slot0].len = 0;
        g_guest_pipes[slot0].nonblock = 0;
        g_guest_pipes[slot1].used = 1;
        g_guest_pipes[slot1].rd_open = 1;
        g_guest_pipes[slot1].wr_open = 1;
        g_guest_pipes[slot1].len = 0;
        g_guest_pipes[slot1].nonblock = 0;
        rd0 = bfree_guest_pipe_magic_fd(slot0, 0);
        wr0 = bfree_guest_pipe_magic_fd(slot0, 1);
        rd1 = bfree_guest_pipe_magic_fd(slot1, 0);
        wr1 = bfree_guest_pipe_magic_fd(slot1, 1);
        if (bfree_sock_accept_q_push(g_unix_socks[li].q_rd,
                                     g_unix_socks[li].q_wr,
                                     &g_unix_socks[li].q_len,
                                     g_unix_socks[li].listen_backlog,
                                     rd0, wr1) != 0) {
            g_guest_pipes[slot0].used = 0;
            g_guest_pipes[slot1].used = 0;
            return -11;
        }
        g_unix_socks[idx].connected = 1;
        g_unix_socks[idx].pipe_magic = wr0;
        g_unix_socks[idx].accept_rd = rd1;
        g_unix_socks[idx].accept_wr = -1;
        return 0;
    }
}

static long sys_linux_accept(long sockfd, long addr, long addrlen)
{
    int idx, rd, wr, ni, pub;
    int spins;
    (void)addr;
    (void)addrlen;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (!g_inet_socks[idx].listening) {
            return -22;
        }
        if (g_inet_socks[idx].q_len <= 0) {
            if (g_inet_socks[idx].nonblock) {
                return -11; /* EAGAIN */
            }
            for (spins = 0; spins < 200000; ++spins) {
                bfree_guest_alarm_poll();
                if (g_inet_socks[idx].q_len > 0) {
                    break;
                }
                __asm__ volatile("sti; hlt" ::: "memory");
            }
            if (g_inet_socks[idx].q_len <= 0) {
                return -11;
            }
        }
        if (bfree_sock_accept_q_pop(g_inet_socks[idx].q_rd,
                                    g_inet_socks[idx].q_wr,
                                    &g_inet_socks[idx].q_len, &rd, &wr) != 0) {
            return -11;
        }
        for (ni = 0; ni < BFREE_INET_SLOTS; ++ni) {
            if (!g_inet_socks[ni].used) {
                break;
            }
        }
        if (ni >= BFREE_INET_SLOTS) {
            return -24;
        }
        g_inet_socks[ni].used = 1;
        g_inet_socks[ni].listening = 0;
        g_inet_socks[ni].connected = 1;
        g_inet_socks[ni].bound = 1;
        g_inet_socks[ni].is_dgram = 0;
        g_inet_socks[ni].is_v6 = g_inet_socks[idx].is_v6;
        g_inet_socks[ni].is_raw = 0;
        g_inet_socks[ni].ip_proto = 0;
        g_inet_socks[ni].shut_rd = 0;
        g_inet_socks[ni].shut_wr = 0;
        g_inet_socks[ni].nonblock = 0;
        g_inet_socks[ni].so_reuseaddr = g_inet_socks[idx].so_reuseaddr;
        g_inet_socks[ni].so_reuseport = g_inet_socks[idx].so_reuseport;
        g_inet_socks[ni].so_keepalive = g_inet_socks[idx].so_keepalive;
        g_inet_socks[ni].so_broadcast = g_inet_socks[idx].so_broadcast;
        g_inet_socks[ni].so_linger_on = g_inet_socks[idx].so_linger_on;
        g_inet_socks[ni].so_linger_sec = g_inet_socks[idx].so_linger_sec;
        g_inet_socks[ni].so_oobinline = g_inet_socks[idx].so_oobinline;
        g_inet_socks[ni].so_error = 0;
        g_inet_socks[ni].tcp_nodelay = g_inet_socks[idx].tcp_nodelay;
        g_inet_socks[ni].ip_ttl = g_inet_socks[idx].ip_ttl > 0
                                      ? g_inet_socks[idx].ip_ttl
                                      : 64;
        g_inet_socks[ni].ip_tos = g_inet_socks[idx].ip_tos;
        g_inet_socks[ni].so_rcvbuf = g_inet_socks[idx].so_rcvbuf;
        g_inet_socks[ni].so_sndbuf = g_inet_socks[idx].so_sndbuf;
        g_inet_socks[ni].so_rcvtimeo_us = g_inet_socks[idx].so_rcvtimeo_us;
        g_inet_socks[ni].so_sndtimeo_us = g_inet_socks[idx].so_sndtimeo_us;
        g_inet_socks[ni].addr = g_inet_socks[idx].addr;
        g_inet_socks[ni].port = g_inet_socks[idx].port;
        g_inet_socks[ni].accept_rd = rd;
        g_inet_socks[ni].accept_wr = -1;
        g_inet_socks[ni].pipe_magic = wr;
        g_inet_socks[ni].tcp_pcb = -1;
        g_inet_socks[ni].peer_addr = 0;
        g_inet_socks[ni].peer_port = 0;
        g_inet_socks[ni].dg_head = 0;
        g_inet_socks[ni].dg_count = 0;
        bfree_sock_accept_q_reset(g_inet_socks[ni].q_rd, g_inet_socks[ni].q_wr,
                                  &g_inet_socks[ni].q_len,
                                  &g_inet_socks[ni].listen_backlog);
        pub = bfree_guest_fd_publish((int)BFREE_INET_FD_BASE + ni);
        if (pub < 0) {
            g_inet_socks[ni].used = 0;
            return pub;
        }
        return pub;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (!g_unix_socks[idx].listening) {
        return -22;
    }
    if (g_unix_socks[idx].q_len <= 0) {
        if (g_unix_socks[idx].nonblock) {
            return -11;
        }
        for (spins = 0; spins < 200000; ++spins) {
            bfree_guest_alarm_poll();
            if (g_unix_socks[idx].q_len > 0) {
                break;
            }
            __asm__ volatile("sti; hlt" ::: "memory");
        }
        if (g_unix_socks[idx].q_len <= 0) {
            return -11;
        }
    }
    if (bfree_sock_accept_q_pop(g_unix_socks[idx].q_rd, g_unix_socks[idx].q_wr,
                                &g_unix_socks[idx].q_len, &rd, &wr) != 0) {
        return -11;
    }
    for (ni = 0; ni < BFREE_UNIX_SLOTS; ++ni) {
        if (!g_unix_socks[ni].used) {
            break;
        }
    }
    if (ni >= BFREE_UNIX_SLOTS) {
        return -24;
    }
    g_unix_socks[ni].used = 1;
    g_unix_socks[ni].listening = 0;
    g_unix_socks[ni].connected = 1;
    g_unix_socks[ni].is_dgram = 0;
    g_unix_socks[ni].shut_rd = 0;
    g_unix_socks[ni].shut_wr = 0;
    g_unix_socks[ni].nonblock = 0;
    g_unix_socks[ni].so_keepalive = g_unix_socks[idx].so_keepalive;
    g_unix_socks[ni].so_rcvbuf = g_unix_socks[idx].so_rcvbuf;
    g_unix_socks[ni].so_sndbuf = g_unix_socks[idx].so_sndbuf;
    g_unix_socks[ni].accept_rd = rd;
    g_unix_socks[ni].accept_wr = -1;
    g_unix_socks[ni].pipe_magic = wr;
    g_unix_socks[ni].path[0] = '\0';
    g_unix_socks[ni].peer_path[0] = '\0';
    g_unix_socks[ni].dg_src[0] = '\0';
    g_unix_socks[ni].dg_pending = 0;
    g_unix_socks[ni].dg_len = 0;
    bfree_sock_accept_q_reset(g_unix_socks[ni].q_rd, g_unix_socks[ni].q_wr,
                              &g_unix_socks[ni].q_len,
                              &g_unix_socks[ni].listen_backlog);
    pub = bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + ni);
    if (pub < 0) {
        g_unix_socks[ni].used = 0;
        return pub;
    }
    return pub;
}


/* B: stub DNS (UDP/53 → 10.0.2.3) answered from /etc/hosts table. */
#define BFREE_DNS_NS_ADDR 0x0A000203U /* 10.0.2.3 */

static int bfree_hosts_lookup_a(const char *qname, uint32_t *out_addr)
{
    const char *p = g_guest_etc_hosts;
    char ip[32];
    char host[64];
    size_t i, j;

    if (!qname || !out_addr) {
        return 0;
    }
    while (*p) {
        i = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i + 1U < sizeof(ip)) {
            ip[i++] = *p++;
        }
        ip[i] = '\0';
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        j = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && j + 1U < sizeof(host)) {
            host[j++] = *p++;
        }
        host[j] = '\0';
        while (*p && *p != '\n') {
            ++p;
        }
        if (*p == '\n') {
            ++p;
        }
        if (ip[0] == '\0' || host[0] == '\0' || ip[0] == ':') {
            continue; /* skip IPv6 lines */
        }
        if (strcmp(host, qname) == 0) {
            unsigned a = 0, b = 0, c = 0, d = 0;
            const char *s = ip;
            a = 0;
            while (*s >= '0' && *s <= '9') {
                a = a * 10U + (unsigned)(*s - '0');
                ++s;
            }
            if (*s != '.') {
                continue;
            }
            ++s;
            b = 0;
            while (*s >= '0' && *s <= '9') {
                b = b * 10U + (unsigned)(*s - '0');
                ++s;
            }
            if (*s != '.') {
                continue;
            }
            ++s;
            c = 0;
            while (*s >= '0' && *s <= '9') {
                c = c * 10U + (unsigned)(*s - '0');
                ++s;
            }
            if (*s != '.') {
                continue;
            }
            ++s;
            d = 0;
            while (*s >= '0' && *s <= '9') {
                d = d * 10U + (unsigned)(*s - '0');
                ++s;
            }
            *out_addr = (a << 24) | (b << 16) | (c << 8) | d;
            return 1;
        }
    }
    return 0;
}

static long bfree_dns_stub_reply(int sender, long buf, size_t n)
{
    const uint8_t *q;
    uint8_t resp[512];
    size_t o = 0;
    size_t qi;
    char qname[64];
    size_t qn = 0;
    uint32_t addr = BFREE_INADDR_LOOPBACK;
    int found;
    bfree_inet_sock_t *ss;
    int tail;

    if (n < 12 || n > BFREE_INET_DGRAM_SIZE) {
        return (long)n;
    }
    q = (const uint8_t *)(uintptr_t)buf;
    /* Copy header; set QR|AA|RD, ANCOUNT=1 or 0 */
    memcpy(resp, q, 12);
    resp[2] = (uint8_t)(0x80 | (q[2] & 0x01) | 0x04); /* QR + copy RD + AA */
    resp[3] = 0x00;
    resp[6] = 0;
    resp[7] = 0; /* ANCOUNT filled later */
    o = 12;
    qi = 12;
    while (qi < n && q[qi] != 0) {
        unsigned lab = q[qi++];
        if (lab > 63 || qi + lab > n) {
            return (long)n;
        }
        if (qn && qn + 1U < sizeof(qname)) {
            qname[qn++] = '.';
        }
        while (lab-- > 0 && qi < n && qn + 1U < sizeof(qname)) {
            qname[qn++] = (char)q[qi++];
        }
    }
    qname[qn] = '\0';
    if (qi < n && q[qi] == 0) {
        ++qi;
    }
    /* copy question */
    if (qi + 4 > n || o + (qi - 12) + 4 > sizeof(resp)) {
        return (long)n;
    }
    memcpy(resp + o, q + 12, qi - 12 + 4);
    o += qi - 12 + 4;
    found = bfree_hosts_lookup_a(qname, &addr);
    if (!found) {
        found = bfree_hosts_lookup_a("localhost", &addr);
    }
    if (found && o + 16 <= sizeof(resp)) {
        resp[6] = 0;
        resp[7] = 1;
        resp[o++] = 0xc0;
        resp[o++] = 0x0c; /* pointer to QNAME */
        resp[o++] = 0x00;
        resp[o++] = 0x01; /* A */
        resp[o++] = 0x00;
        resp[o++] = 0x01; /* IN */
        resp[o++] = 0x00;
        resp[o++] = 0x00;
        resp[o++] = 0x00;
        resp[o++] = 0x3c; /* TTL 60 */
        resp[o++] = 0x00;
        resp[o++] = 0x04;
        resp[o++] = (uint8_t)((addr >> 24) & 0xff);
        resp[o++] = (uint8_t)((addr >> 16) & 0xff);
        resp[o++] = (uint8_t)((addr >> 8) & 0xff);
        resp[o++] = (uint8_t)(addr & 0xff);
    } else {
        resp[3] = 0x03; /* NXDOMAIN-ish RCODE */
    }
    ss = &g_inet_socks[sender];
    if (ss->dg_count >= BFREE_INET_DGRAMS) {
        return (long)n;
    }
    tail = (ss->dg_head + ss->dg_count) % BFREE_INET_DGRAMS;
    memcpy(ss->dg_buf[tail], resp, o);
    ss->dg_len[tail] = (uint16_t)o;
    ss->dg_src_addr[tail] = BFREE_DNS_NS_ADDR;
    ss->dg_src_port[tail] = 53;
    ss->dg_count++;
    return (long)n;
}

/* F2: e1000/udp stack delivers into the bound guest SOCK_DGRAM queue. */
#define BFREE_UDP_PORT_CB_MAX 8
static uint16_t g_udp_cb_ports[BFREE_UDP_PORT_CB_MAX];

static void bfree_inet_udp_enqueue(uint16_t dst_port, uint32_t src_ip_le, uint16_t src_port,
                                  const uint8_t *data, size_t len)
{
    int r;
    uint32_t src_guest = bfree_inet_ntohl(src_ip_le);

    if (!data) {
        return;
    }
    if (len > BFREE_INET_DGRAM_SIZE) {
        len = BFREE_INET_DGRAM_SIZE;
    }
    for (r = 0; r < BFREE_INET_SLOTS; ++r) {
        bfree_inet_sock_t *rs = &g_inet_socks[r];
        int tail;

        if (!rs->used || !rs->is_dgram || !rs->bound || rs->port != dst_port) {
            continue;
        }
        if (rs->dg_count >= BFREE_INET_DGRAMS) {
            break;
        }
        tail = (rs->dg_head + rs->dg_count) % BFREE_INET_DGRAMS;
        memcpy(rs->dg_buf[tail], data, len);
        rs->dg_len[tail] = (uint16_t)len;
        rs->dg_src_addr[tail] = src_guest;
        rs->dg_src_port[tail] = src_port;
        rs->dg_count++;
        break;
    }
}

static void bfree_inet_udp_cb0(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[0], a, p, d, n); }
static void bfree_inet_udp_cb1(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[1], a, p, d, n); }
static void bfree_inet_udp_cb2(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[2], a, p, d, n); }
static void bfree_inet_udp_cb3(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[3], a, p, d, n); }
static void bfree_inet_udp_cb4(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[4], a, p, d, n); }
static void bfree_inet_udp_cb5(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[5], a, p, d, n); }
static void bfree_inet_udp_cb6(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[6], a, p, d, n); }
static void bfree_inet_udp_cb7(uint32_t a, uint16_t p, const uint8_t *d, size_t n)
{ bfree_inet_udp_enqueue(g_udp_cb_ports[7], a, p, d, n); }

typedef void (*bfree_udp_cb_fn)(uint32_t, uint16_t, const uint8_t *, size_t);
static bfree_udp_cb_fn g_udp_cb_fns[BFREE_UDP_PORT_CB_MAX] = {
    bfree_inet_udp_cb0, bfree_inet_udp_cb1, bfree_inet_udp_cb2, bfree_inet_udp_cb3,
    bfree_inet_udp_cb4, bfree_inet_udp_cb5, bfree_inet_udp_cb6, bfree_inet_udp_cb7
};

static void bfree_inet_udp_bind_stack(uint16_t port)
{
    int i;

    for (i = 0; i < BFREE_UDP_PORT_CB_MAX; ++i) {
        if (g_udp_cb_ports[i] == port) {
            udp_register_port(port, g_udp_cb_fns[i]);
            return;
        }
    }
    for (i = 0; i < BFREE_UDP_PORT_CB_MAX; ++i) {
        if (g_udp_cb_ports[i] == 0) {
            g_udp_cb_ports[i] = port;
            udp_register_port(port, g_udp_cb_fns[i]);
            return;
        }
    }
}

static void bfree_inet_udp_unbind_stack(uint16_t port)
{
    int i;
    int still = 0;

    for (i = 0; i < BFREE_INET_SLOTS; ++i) {
        if (g_inet_socks[i].used && g_inet_socks[i].is_dgram &&
            g_inet_socks[i].bound && g_inet_socks[i].port == port) {
            still = 1;
            break;
        }
    }
    if (still) {
        return;
    }
    udp_unregister_port(port);
    for (i = 0; i < BFREE_UDP_PORT_CB_MAX; ++i) {
        if (g_udp_cb_ports[i] == port) {
            g_udp_cb_ports[i] = 0;
        }
    }
}

/* F2: deliver one UDP datagram to a bound loopback receiver, else e1000. */
static long bfree_inet_dgram_send(int sender, uint32_t dst_addr, uint16_t dst_port,
                                  long buf, long len)
{
    int r;
    size_t n;
    int delivered = 0;
    uint16_t src_port;
    uint32_t src_addr;

    if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if (!bfree_inet_is_guest_routable(dst_addr)) {
        return -101; /* ENETUNREACH */
    }
    n = (size_t)len;
    if (n > BFREE_INET_DGRAM_SIZE) {
        return -90; /* EMSGSIZE */
    }
    /* B: nameserver stub (slirp DNS). */
    if (dst_port == 53 && dst_addr == BFREE_DNS_NS_ADDR) {
        return bfree_dns_stub_reply(sender, buf, n);
    }
    src_addr = g_inet_socks[sender].bound
                   ? g_inet_socks[sender].addr
                   : BFREE_INADDR_LOOPBACK;
    if (src_addr == BFREE_INADDR_ANY) {
        src_addr = BFREE_INADDR_GUEST_LAN;
    }
    src_port = g_inet_socks[sender].bound
                   ? g_inet_socks[sender].port
                   : (uint16_t)(40000 + sender);

    for (r = 0; r < BFREE_INET_SLOTS; ++r) {
        bfree_inet_sock_t *rs = &g_inet_socks[r];
        int tail;

        if (!rs->used || !rs->is_dgram || !rs->bound || rs->port != dst_port) {
            continue;
        }
        if (rs->dg_count >= BFREE_INET_DGRAMS) {
            break; /* receiver queue full: drop (UDP) */
        }
        tail = (rs->dg_head + rs->dg_count) % BFREE_INET_DGRAMS;
        memcpy(rs->dg_buf[tail], (const void *)(uintptr_t)buf, n);
        rs->dg_len[tail] = (uint16_t)n;
        rs->dg_src_addr[tail] = src_addr;
        rs->dg_src_port[tail] = src_port;
        rs->dg_count++;
        delivered = 1;
        break;
    }
    if (delivered) {
        return (long)n;
    }
    /* No local receiver: send via e1000/udp stack (10.0.2/24 LAN). */
    if ((dst_addr & 0xFFFFFF00u) == 0x0A000200u) {
        int rc = udp_send(bfree_inet_ntohl(dst_addr), dst_port, src_port,
                          (const uint8_t *)(uintptr_t)buf, n);
        if (rc < 0) {
            return -101; /* ENETUNREACH / TX fail */
        }
        return (long)n;
    }
    return (long)n; /* UDP: success even if no receiver (dropped) */
}

static long bfree_inet_raw_sendto(int idx, uint32_t dst_addr, long buf, long len)
{
    int spins;
    int rc;

    if (buf == 0 || len <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if (!bfree_inet_is_guest_routable(dst_addr)) {
        return -101;
    }
    if (!g_inet_socks[idx].is_raw) {
        return -95; /* EOPNOTSUPP */
    }
    /* BusyBox ping: SOCK_RAW + IPPROTO_ICMP payload (no IP header). */
    if (g_inet_socks[idx].ip_proto != 0 &&
        g_inet_socks[idx].ip_proto != BFREE_IPPROTO_ICMP) {
        return -93; /* EPROTONOSUPPORT */
    }
    for (spins = 0; spins < 4096; ++spins) {
        rc = ipv4_send(bfree_inet_ntohl(dst_addr), (uint8_t)BFREE_IPPROTO_ICMP,
                       (const uint8_t *)(uintptr_t)buf, (size_t)len);
        if (rc == NET_SEND_OK) {
            return len;
        }
        if (rc == NET_SEND_PENDING) {
            net_runtime_poll();
            continue;
        }
        if (rc == NET_SEND_TIMEOUT || rc == NET_SEND_NO_ROUTE) {
            return -101;
        }
        return -5; /* EIO */
    }
    return -11; /* EAGAIN — ARP not resolved */
}

static long bfree_inet_raw_recvfrom(int idx, long buf, long len, long addr)
{
    uint8_t icmp_buf[1500];
    uint32_t src_le = 0;
    uint32_t src_guest;
    int n = 0;
    int spins;
    size_t out;
    uint8_t *dst;
    uint16_t tot_be;
    uint32_t sbe;
    uint32_t dbe;

    (void)idx;
    if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    for (spins = 0; spins < 200000; ++spins) {
        n = icmp_recv(&src_le, icmp_buf, sizeof(icmp_buf));
        if (n > 0) {
            break;
        }
        net_runtime_poll();
    }
    if (n <= 0) {
        return -11; /* EAGAIN */
    }
    /* Linux SOCK_RAW(IPPROTO_ICMP) delivers IP header + ICMP. */
    out = 20U + (size_t)n;
    if (out > (size_t)len) {
        out = (size_t)len;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    memset(dst, 0, out);
    if (out >= 20U) {
        dst[0] = 0x45; /* v4, ihl=5 */
        tot_be = bfree_inet_ntohs((uint16_t)(20 + n));
        memcpy(dst + 2, &tot_be, 2);
        dst[8] = 64;
        dst[9] = (uint8_t)BFREE_IPPROTO_ICMP;
        src_guest = bfree_inet_ntohl(src_le);
        sbe = bfree_inet_ntohl(src_guest);
        dbe = bfree_inet_ntohl(BFREE_INADDR_GUEST_LAN);
        memcpy(dst + 12, &sbe, 4);
        memcpy(dst + 16, &dbe, 4);
        if (out > 20U) {
            size_t copy = out - 20U;
            if (copy > (size_t)n) {
                copy = (size_t)n;
            }
            memcpy(dst + 20, icmp_buf, copy);
        }
    }
    if (addr != 0 && bfree_user_ptr_mapped(addr)) {
        uint8_t *sa = (uint8_t *)(uintptr_t)addr;
        uint32_t abe;
        src_guest = bfree_inet_ntohl(src_le);
        abe = bfree_inet_ntohl(src_guest);
        sa[0] = 2;
        sa[1] = 0;
        sa[2] = 0;
        sa[3] = 0;
        memcpy(sa + 4, &abe, 4);
        memset(sa + 8, 0, 8);
    }
    return (long)out;
}

/* Copy the sun_path out of a user sockaddr_un (offset 2, NUL-terminated). */
static int bfree_unix_copy_sun_path(long addr, long addrlen, char *out, size_t cap)
{
    const uint8_t *raw;
    size_t n = 0;
    size_t limit = (addrlen > 0) ? (size_t)addrlen : 0;

    if (addr == 0 || limit < 3U || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    while (n + 2U < limit && n + 1U < cap && raw[2 + n] != 0) {
        out[n] = (char)raw[2 + n];
        ++n;
    }
    out[n] = '\0';
    return (n == 0U) ? -22 : 0;
}

static int bfree_unix_find_bound(const char *path)
{
    int i;

    for (i = 0; i < BFREE_UNIX_SLOTS; ++i) {
        if (g_unix_socks[i].used && g_unix_socks[i].is_dgram &&
            g_unix_socks[i].path[0] != '\0' &&
            strcmp(g_unix_socks[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/* AF_UNIX SOCK_DGRAM: one in-flight datagram per receiver mailbox. */
static long bfree_unix_dgram_send(int idx, long buf, long len, long addr,
                                  long addrlen)
{
    char path[96];
    bfree_unix_sock_t *rs;
    int ri;
    size_t n;
    size_t i;
    const uint8_t *src;

    if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if ((size_t)len > BFREE_UNIX_DGRAM_SIZE) {
        return -90; /* EMSGSIZE */
    }
    if (addr != 0) {
        int rc = bfree_unix_copy_sun_path(addr, addrlen ? addrlen : 110,
                                          path, sizeof(path));
        if (rc != 0) {
            return rc;
        }
    } else if (g_unix_socks[idx].connected &&
               g_unix_socks[idx].peer_path[0] != '\0') {
        for (i = 0; g_unix_socks[idx].peer_path[i] != '\0' &&
                    i + 1U < sizeof(path); ++i) {
            path[i] = g_unix_socks[idx].peer_path[i];
        }
        path[i] = '\0';
    } else {
        return -89; /* EDESTADDRREQ */
    }
    ri = bfree_unix_find_bound(path);
    if (ri < 0) {
        return -111; /* ECONNREFUSED — nothing bound there */
    }
    rs = &g_unix_socks[ri];
    if (rs->dg_pending) {
        return -11; /* EAGAIN — mailbox full */
    }
    n = (size_t)len;
    src = (const uint8_t *)(uintptr_t)buf;
    for (i = 0; i < n; ++i) {
        rs->dg_buf[i] = src[i];
    }
    rs->dg_len = n;
    rs->dg_pending = 1;
    for (i = 0; g_unix_socks[idx].path[i] != '\0' &&
                i + 1U < sizeof(rs->dg_src); ++i) {
        rs->dg_src[i] = g_unix_socks[idx].path[i];
    }
    rs->dg_src[i] = '\0';
    return (long)n;
}

static long bfree_unix_dgram_recv(int idx, long buf, long len, int peek,
                                  int dontwait, long addr)
{
    bfree_unix_sock_t *s = &g_unix_socks[idx];
    size_t n;
    size_t i;
    uint8_t *dst;
    int spins;

    if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    /* Bounded wait: the sender is the same guest thread in every practical
     * case, so a long spin here would only look like a hang. */
    if (!s->dg_pending && !(s->nonblock || dontwait)) {
        for (spins = 0; spins < 4096 && !s->dg_pending; ++spins) {
            bfree_guest_alarm_poll();
            if (s->dg_pending) {
                break;
            }
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
    if (!s->dg_pending) {
        return -11; /* EAGAIN */
    }
    n = s->dg_len;
    if (n > (size_t)len) {
        n = (size_t)len;
    }
    dst = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < n; ++i) {
        dst[i] = s->dg_buf[i];
    }
    if (addr != 0 && bfree_user_ptr_mapped(addr)) {
        uint8_t *sa = (uint8_t *)(uintptr_t)addr;

        sa[0] = (uint8_t)BFREE_LINUX_AF_UNIX;
        sa[1] = 0;
        for (i = 0; s->dg_src[i] != '\0' && i < 106U; ++i) {
            sa[2 + i] = (uint8_t)s->dg_src[i];
        }
        sa[2 + i] = 0;
    }
    if (!peek) {
        s->dg_pending = 0;
        s->dg_len = 0;
    }
    return (long)n;
}

static long sys_linux_sendto(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    /* MSG_OOB is sent in-band; MSG_CMSG_CLOEXEC is meaningless without SCM. */
    flags = (long)((unsigned long)flags & ~BFREE_MSG_SOFT_IGNORED);
    (void)flags;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].shut_wr) {
        bfree_guest_sig_raise(13); /* SIGPIPE */
        return -32; /* EPIPE */
    }
    if (idx >= 0 && g_inet_socks[idx].is_raw) {
        uint32_t dst_addr;
        uint16_t dst_port;
        if (addr != 0) {
            long perr = bfree_inet_parse_sockaddr(
                addr, g_inet_socks[idx].is_v6 ? 28 : 16, &dst_addr, &dst_port);
            if (perr != 0) {
                return perr;
            }
            (void)dst_port;
        } else if (g_inet_socks[idx].connected) {
            dst_addr = g_inet_socks[idx].peer_addr;
        } else {
            return -89; /* EDESTADDRREQ */
        }
        return bfree_inet_raw_sendto(idx, dst_addr, buf, len);
    }
    if (idx >= 0 && g_inet_socks[idx].is_dgram) {
        uint32_t dst_addr;
        uint16_t dst_port;

        if (addr != 0) {
            /* Dispatch drops arg6; sockaddr_in is 16 bytes, sockaddr_in6 28. */
            long perr = bfree_inet_parse_sockaddr(
                addr, g_inet_socks[idx].is_v6 ? 28 : 16, &dst_addr, &dst_port);
            if (perr != 0) {
                return perr;
            }
        } else if (g_inet_socks[idx].connected) {
            dst_addr = g_inet_socks[idx].peer_addr;
            dst_port = g_inet_socks[idx].peer_port;
        } else {
            return -89; /* EDESTADDRREQ */
        }
        return bfree_inet_dgram_send(idx, dst_addr, dst_port, buf, len);
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].tcp_pcb >= 0) {
        int pcb = g_inet_socks[idx].tcp_pcb;
        int n;

        if (buf == 0 || len <= 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        n = tcp_min_send(pcb, (const uint8_t *)(uintptr_t)buf, (size_t)len);
        return (long)n;
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_inet_socks[idx].pipe_magic, buf, len);
    }
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].shut_wr) {
        bfree_guest_sig_raise(13); /* SIGPIPE */
        return -32; /* EPIPE */
    }
    if (idx >= 0 && g_unix_socks[idx].is_dgram) {
        return bfree_unix_dgram_send(idx, buf, len, addr, addrlen);
    }
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_unix_socks[idx].pipe_magic, buf, len);
    }
    return sys_linux_write(fd, buf, len);
}

static long sys_linux_recvfrom(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    int peek;
    int dontwait;
    int waitall;
    int trunc;

    /* MSG_OOB reads the in-band stream; MSG_CMSG_CLOEXEC has no SCM to apply. */
    flags = (long)((unsigned long)flags & ~BFREE_MSG_SOFT_IGNORED);
    peek = ((unsigned long)flags & 2UL) != 0UL; /* MSG_PEEK */
    dontwait = ((unsigned long)flags & (unsigned long)BFREE_MSG_DONTWAIT) != 0UL;
    waitall = ((unsigned long)flags & (unsigned long)BFREE_MSG_WAITALL) != 0UL;
    trunc = ((unsigned long)flags & (unsigned long)BFREE_MSG_TRUNC) != 0UL;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].shut_rd) {
        return 0;
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);
        if (uidx >= 0 && g_unix_socks[uidx].shut_rd) {
            return 0;
        }
    }
    if (idx >= 0 && g_inet_socks[idx].is_raw) {
        return bfree_inet_raw_recvfrom(idx, buf, len, addr);
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].tcp_pcb >= 0) {
        int pcb = g_inet_socks[idx].tcp_pcb;
        int n;
        int spins;
        int max_spins = (g_inet_socks[idx].nonblock || dontwait) ? 1 : 20000;
        long total = 0;

        if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        for (;;) {
            for (spins = 0; spins < max_spins; ++spins) {
                n = tcp_min_recv(pcb, (uint8_t *)(uintptr_t)(buf + total),
                                 (size_t)(len - total));
                if (n != -11) {
                    break;
                }
                net_runtime_poll();
            }
            if (n == -11) {
                return total > 0 ? total : -11;
            }
            if (n <= 0) {
                return total > 0 ? total : (long)n;
            }
            total += n;
            if (!waitall || total >= len || peek) {
                return total;
            }
        }
    }
    if (idx >= 0 && g_inet_socks[idx].is_dgram) {
        bfree_inet_sock_t *s = &g_inet_socks[idx];
        size_t n;
        size_t full;
        int h;
        int spins;
        int max_spins;

        if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        max_spins = (s->nonblock || dontwait) ? 8 : 200000;
        for (spins = 0; s->dg_count == 0 && spins < max_spins; ++spins) {
            net_runtime_poll();
            if (!(s->nonblock || dontwait)) {
                bfree_guest_alarm_poll();
                if (s->dg_count == 0) {
                    __asm__ volatile("sti; hlt" ::: "memory");
                }
            }
        }
        /* connect()ed UDP only sees traffic from the registered peer; drop
         * anything else off the head of the queue (Linux filters at input). */
        if (s->connected && s->peer_port != 0) {
            while (s->dg_count > 0) {
                int qh = s->dg_head;

                if (s->dg_src_port[qh] == s->peer_port &&
                    (s->peer_addr == BFREE_INADDR_ANY ||
                     s->dg_src_addr[qh] == s->peer_addr)) {
                    break;
                }
                s->dg_head = (qh + 1) % BFREE_INET_DGRAMS;
                s->dg_count--;
            }
        }
        if (s->dg_count == 0) {
            return -11; /* EAGAIN — no datagram queued */
        }
        h = s->dg_head;
        full = s->dg_len[h];
        n = full;
        if (n > (size_t)len) {
            n = (size_t)len; /* truncate into buffer */
        }
        memcpy((void *)(uintptr_t)buf, s->dg_buf[h], n);
        if (addr != 0 && bfree_user_ptr_mapped(addr)) {
            uint8_t *sa = (uint8_t *)(uintptr_t)addr;
            uint16_t pbe = bfree_inet_ntohs(s->dg_src_port[h]);
            uint32_t abe = bfree_inet_ntohl(s->dg_src_addr[h]);
            sa[0] = 2; sa[1] = 0; /* AF_INET LE */
            memcpy(sa + 2, &pbe, 2);
            memcpy(sa + 4, &abe, 4);
            memset(sa + 8, 0, 8);
        }
        if (!peek) {
            s->dg_head = (h + 1) % BFREE_INET_DGRAMS;
            s->dg_count--;
        }
        /* MSG_TRUNC: report full datagram length even if buffer was short. */
        return trunc ? (long)full : (long)n;
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].tcp_pcb < 0) {
        /* Loopback stream: read peer pipe (accept_rd), not the local write slot. */
        int mag = g_inet_socks[idx].accept_rd;
        if (mag < 0 && g_inet_socks[idx].pipe_magic >= 0) {
            mag = g_inet_socks[idx].pipe_magic;
            if (bfree_guest_pipe_is_wr_magic(mag)) {
                mag = mag - 1;
            }
        }
        if (mag >= 0) {
            if (peek) {
                return bfree_guest_pipe_peek(mag, buf, len);
            }
            if (waitall && len > 0) {
                long total = 0;
                while (total < len) {
                    long n = sys_linux_read(mag, buf + total, len - total);
                    if (n < 0) {
                        return total > 0 ? total : n;
                    }
                    if (n == 0) {
                        return total;
                    }
                    total += n;
                }
                return total;
            }
            return sys_linux_read(mag, buf, len);
        }
    }
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].is_dgram) {
        return bfree_unix_dgram_recv(idx, buf, len, peek, dontwait, addr);
    }
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        /* Prefer peer read end (socketpair); else same-slot rd (connect client). */
        int mag = g_unix_socks[idx].accept_rd;
        if (mag < 0) {
            mag = g_unix_socks[idx].pipe_magic;
            if (bfree_guest_pipe_is_wr_magic(mag)) {
                mag = mag - 1;
            }
        }
        if (peek) {
            return bfree_guest_pipe_peek(mag, buf, len);
        }
        if (waitall && len > 0) {
            long total = 0;
            while (total < len) {
                long n = sys_linux_read(mag, buf + total, len - total);
                if (n < 0) {
                    return total > 0 ? total : n;
                }
                if (n == 0) {
                    return total;
                }
                total += n;
            }
            return total;
        }
        return sys_linux_read(mag, buf, len);
    }
    if (peek && bfree_guest_is_pipe_rd((int)fd)) {
        return bfree_guest_pipe_peek((int)fd, buf, len);
    }
    return sys_linux_read(fd, buf, len);
}

/* Linux x86_64 struct msghdr (userspace layout); iovec typedef is above. */
typedef struct {
    uint64_t msg_name;
    uint32_t msg_namelen;
    uint32_t _pad0;
    uint64_t msg_iov;
    uint64_t msg_iovlen;
    uint64_t msg_control;
    uint64_t msg_controllen;
    uint32_t msg_flags;
    uint32_t _pad1;
} bfree_linux_msghdr_t;

/* 46/47: no cmsg support; iovecs are gathered/scattered through a bounce buffer. */
#define BFREE_MSG_IOV_MAX   8    /* iovecs actually gathered/scattered */
#define BFREE_LINUX_IOV_MAX 1024 /* above this Linux reports EMSGSIZE */
#define BFREE_MSG_BOUNCE    2048

static uint8_t g_msg_bounce[BFREE_MSG_BOUNCE];

/* Datagram-ish sockets need one send per message, so gather first. */
static int bfree_sock_is_datagram(long fd)
{
    int resolved = bfree_guest_fd_resolve((int)fd);
    int iidx = bfree_inet_from_fd(resolved);
    int uidx;

    if (iidx >= 0) {
        return (g_inet_socks[iidx].is_dgram || g_inet_socks[iidx].is_raw);
    }
    uidx = bfree_unix_from_fd(resolved);
    if (uidx >= 0) {
        return g_unix_socks[uidx].is_dgram;
    }
    return 0;
}

static long sys_linux_sendmsg(long fd, long msg_ptr, long flags)
{
    const bfree_linux_msghdr_t *mh;
    const bfree_linux_iovec_t *iov;
    uint64_t nio;
    uint64_t i;
    size_t total = 0;

    if (msg_ptr == 0 || !bfree_user_ptr_mapped(msg_ptr)) {
        return -14;
    }
    /* Ancillary data (msg_control) is not carried; MSG_OOB goes in-band. */
    flags = (long)((unsigned long)flags & ~BFREE_MSG_SOFT_IGNORED);
    mh = (const bfree_linux_msghdr_t *)(uintptr_t)msg_ptr;
    nio = mh->msg_iovlen;
    if (nio > BFREE_LINUX_IOV_MAX) {
        return -90; /* EMSGSIZE — also catches a negative msg_iovlen */
    }
    if (nio == 0) {
        return 0;
    }
    if (mh->msg_iov == 0 || !bfree_user_ptr_mapped((long)mh->msg_iov)) {
        return -14;
    }
    if (nio > BFREE_MSG_IOV_MAX) {
        nio = BFREE_MSG_IOV_MAX;
    }
    iov = (const bfree_linux_iovec_t *)(uintptr_t)mh->msg_iov;
    if (nio == 1) {
        return sys_linux_sendto(fd, (long)iov[0].iov_base, (long)iov[0].iov_len,
                                flags, (long)mh->msg_name, (long)mh->msg_namelen);
    }
    if (!bfree_sock_is_datagram(fd)) {
        /* Stream: one write per iovec preserves byte order on the wire. */
        long sent = 0;

        for (i = 0; i < nio; ++i) {
            long n;

            if (iov[i].iov_len == 0) {
                continue;
            }
            n = sys_linux_sendto(fd, (long)iov[i].iov_base,
                                 (long)iov[i].iov_len, flags,
                                 (long)mh->msg_name, (long)mh->msg_namelen);
            if (n < 0) {
                return sent > 0 ? sent : n;
            }
            sent += n;
            if ((size_t)n < (size_t)iov[i].iov_len) {
                break; /* short write: stop gathering */
            }
        }
        return sent;
    }
    /* Datagram: gather into the bounce buffer and emit exactly one packet. */
    for (i = 0; i < nio; ++i) {
        const uint8_t *src = (const uint8_t *)(uintptr_t)iov[i].iov_base;
        size_t n = (size_t)iov[i].iov_len;
        size_t k;

        if (n == 0) {
            continue;
        }
        if (src == 0 || !bfree_user_ptr_mapped((long)iov[i].iov_base)) {
            return -14;
        }
        if (n > BFREE_MSG_BOUNCE - total) {
            n = BFREE_MSG_BOUNCE - total;
        }
        for (k = 0; k < n; ++k) {
            g_msg_bounce[total + k] = src[k];
        }
        total += n;
        if (total >= BFREE_MSG_BOUNCE) {
            break;
        }
    }
    return sys_linux_sendto(fd, (long)(uintptr_t)g_msg_bounce, (long)total,
                            flags, (long)mh->msg_name, (long)mh->msg_namelen);
}

static long sys_linux_recvmsg(long fd, long msg_ptr, long flags)
{
    bfree_linux_msghdr_t *mh;
    const bfree_linux_iovec_t *iov;
    uint64_t nio;
    uint64_t i;
    long n;
    size_t want;
    size_t off;

    if (msg_ptr == 0 || !bfree_user_ptr_mapped(msg_ptr)) {
        return -14;
    }
    /* MSG_CMSG_CLOEXEC only applies to SCM_RIGHTS fds, which are never
     * produced here; msg_controllen is reported as 0 on every return path. */
    flags = (long)((unsigned long)flags & ~BFREE_MSG_SOFT_IGNORED);
    mh = (bfree_linux_msghdr_t *)(uintptr_t)msg_ptr;
    nio = mh->msg_iovlen;
    if (nio > BFREE_LINUX_IOV_MAX) {
        return -90; /* EMSGSIZE */
    }
    if (nio == 0) {
        mh->msg_controllen = 0;
        mh->msg_flags = 0;
        return 0;
    }
    if (mh->msg_iov == 0 || !bfree_user_ptr_mapped((long)mh->msg_iov)) {
        return -14;
    }
    if (nio > BFREE_MSG_IOV_MAX) {
        nio = BFREE_MSG_IOV_MAX;
    }
    iov = (const bfree_linux_iovec_t *)(uintptr_t)mh->msg_iov;
    mh->msg_controllen = 0;
    mh->msg_flags = 0;
    if (nio == 1) {
        want = (size_t)iov[0].iov_len;
        n = sys_linux_recvfrom(fd, (long)iov[0].iov_base, (long)iov[0].iov_len,
                               flags, (long)mh->msg_name, 0);
        if (n > 0 && (size_t)n > want) {
            /* MSG_TRUNC path returned full datagram length. */
            mh->msg_flags |= (uint32_t)BFREE_MSG_TRUNC;
        }
        return n;
    }
    /* Multi-iov: receive once into the bounce buffer, then scatter. */
    want = 0;
    for (i = 0; i < nio; ++i) {
        want += (size_t)iov[i].iov_len;
    }
    if (want > BFREE_MSG_BOUNCE) {
        want = BFREE_MSG_BOUNCE;
    }
    if (want == 0) {
        return 0;
    }
    n = sys_linux_recvfrom(fd, (long)(uintptr_t)g_msg_bounce, (long)want,
                           flags, (long)mh->msg_name, 0);
    if (n <= 0) {
        return n;
    }
    if ((size_t)n > want) {
        /* MSG_TRUNC: n is the full datagram length, bounce holds want bytes. */
        mh->msg_flags |= (uint32_t)BFREE_MSG_TRUNC;
        off = want;
    } else {
        off = (size_t)n;
    }
    {
        size_t done = 0;

        for (i = 0; i < nio && done < off; ++i) {
            uint8_t *dst = (uint8_t *)(uintptr_t)iov[i].iov_base;
            size_t cap = (size_t)iov[i].iov_len;
            size_t k;

            if (cap == 0) {
                continue;
            }
            if (dst == 0 || !bfree_user_ptr_mapped((long)iov[i].iov_base)) {
                return -14;
            }
            if (cap > off - done) {
                cap = off - done;
            }
            for (k = 0; k < cap; ++k) {
                dst[k] = g_msg_bounce[done + k];
            }
            done += cap;
        }
    }
    return n;
}

/* Linux x86_64 mmsghdr: msghdr (56) + msg_len (4) + pad → 64. */
typedef struct {
    bfree_linux_msghdr_t msg_hdr;
    uint32_t msg_len;
    uint32_t _pad;
} bfree_linux_mmsghdr_t;

static long sys_linux_sendmmsg(long fd, long mmsg_ptr, long vlen, long flags)
{
    long i;
    long sent = 0;

    if (vlen < 0) {
        return -22;
    }
    if (vlen == 0) {
        return 0;
    }
    if (mmsg_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)mmsg_ptr,
                               (uint64_t)vlen * sizeof(bfree_linux_mmsghdr_t))) {
        return -14;
    }
    for (i = 0; i < vlen; ++i) {
        bfree_linux_mmsghdr_t *mm =
            &((bfree_linux_mmsghdr_t *)(uintptr_t)mmsg_ptr)[i];
        long n = sys_linux_sendmsg(fd, (long)(uintptr_t)&mm->msg_hdr, flags);
        if (n < 0) {
            return sent > 0 ? sent : n;
        }
        mm->msg_len = (uint32_t)n;
        ++sent;
    }
    return sent;
}

static long sys_linux_recvmmsg(long fd, long mmsg_ptr, long vlen, long flags,
                               long timeout_ptr)
{
    long i;
    long got = 0;
    (void)timeout_ptr; /* timeout ignored: single-shot drain */

    if (vlen < 0) {
        return -22;
    }
    if (vlen == 0) {
        return 0;
    }
    if (mmsg_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)mmsg_ptr,
                               (uint64_t)vlen * sizeof(bfree_linux_mmsghdr_t))) {
        return -14;
    }
    for (i = 0; i < vlen; ++i) {
        bfree_linux_mmsghdr_t *mm =
            &((bfree_linux_mmsghdr_t *)(uintptr_t)mmsg_ptr)[i];
        long n = sys_linux_recvmsg(fd, (long)(uintptr_t)&mm->msg_hdr, flags);
        if (n < 0) {
            return got > 0 ? got : n;
        }
        mm->msg_len = (uint32_t)n;
        ++got;
    }
    return got;
}

static long sys_linux_pread64(long fd, long buf, long count, long offset)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    size_t saved;
    long rc;

    if (offset < 0) {
        return -22;
    }
    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (!vf || !ofd) {
        return -29;
    }
    saved = ofd->pos;
    ofd->pos = (size_t)offset;
    rc = sys_linux_read(fd, buf, count);
    ofd->pos = saved;
    return rc;
}

static long sys_linux_pwrite64(long fd, long buf, long count, long offset)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    size_t saved;
    long rc;

    if (offset < 0) {
        return -22;
    }
    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (!vf || !ofd) {
        return -29;
    }
    saved = ofd->pos;
    ofd->pos = (size_t)offset;
    rc = sys_linux_write(fd, buf, count);
    ofd->pos = saved;
    return rc;
}

static long sys_linux_select_ms(long nfds, long readfds, long writefds, long exceptfds,
                                long timeout_ms)
{
    typedef struct {
        int fd;
        short events;
        short revents;
    } bfree_sel_pfd_t;
    bfree_sel_pfd_t pfds[64];
    int n = 0;
    int i;
    long ready = 0;
    uint64_t *rin = 0;
    uint64_t *win = 0;
    uint64_t *ein = 0;
    uint64_t rout[2] = {0, 0};
    uint64_t wout[2] = {0, 0};
    uint64_t eout[2] = {0, 0};
    uint64_t deadline_us = 0;
    int finite = 0;

    if (nfds < 0 || nfds > 128) {
        return -22;
    }
    if (readfds != 0) {
        if (!bfree_user_ptr_mapped(readfds)) {
            return -14;
        }
        rin = (uint64_t *)(uintptr_t)readfds;
    }
    if (writefds != 0) {
        if (!bfree_user_ptr_mapped(writefds)) {
            return -14;
        }
        win = (uint64_t *)(uintptr_t)writefds;
    }
    if (exceptfds != 0) {
        if (!bfree_user_ptr_mapped(exceptfds)) {
            return -14;
        }
        ein = (uint64_t *)(uintptr_t)exceptfds;
    }
    for (i = 0; i < (int)nfds && n < 64; ++i) {
        int word = i / 64;
        int bit = i % 64;
        short ev = 0;
        if (rin && word < 2 && (rin[word] & (1ULL << bit)) != 0ULL) {
            ev = (short)(ev | 0x001);
        }
        if (win && word < 2 && (win[word] & (1ULL << bit)) != 0ULL) {
            ev = (short)(ev | 0x004);
        }
        if (ein && word < 2 && (ein[word] & (1ULL << bit)) != 0ULL) {
            ev = (short)(ev | 0x008);
        }
        if (ev == 0) {
            continue;
        }
        pfds[n].fd = i;
        pfds[n].events = ev;
        pfds[n].revents = 0;
        ++n;
    }
    if (timeout_ms > 0) {
        finite = 1;
        deadline_us = knl_get_current_time() + (uint64_t)timeout_ms * 1000ULL;
    }
    for (;;) {
        int rcount = 0;
        int j;
        int er;
        bfree_guest_alarm_poll();
        er = bfree_guest_sig_take_eintr();
        if (er < 0) {
            return er;
        }
        for (j = 0; j < n; ++j) {
            short rev = bfree_guest_poll_revents(pfds[j].fd, pfds[j].events);
            pfds[j].revents = rev;
            if (rev != 0) {
                ++rcount;
            }
        }
        if (n == 0) {
            if (timeout_ms == 0) {
                break;
            }
            if (timeout_ms < 0) {
                __asm__ volatile("sti; hlt" ::: "memory");
                continue;
            }
        } else if (rcount > 0) {
            ready = rcount;
            break;
        } else if (timeout_ms == 0) {
            break;
        }
        if (finite && knl_get_current_time() >= deadline_us) {
            break;
        }
        if (timeout_ms == 0) {
            break;
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
    for (i = 0; i < n; ++i) {
        int fd = pfds[i].fd;
        int word = fd / 64;
        int bit = fd % 64;
        if (pfds[i].revents == 0 || word >= 2) {
            continue;
        }
        if ((pfds[i].revents & 0x001) != 0) {
            rout[word] |= (1ULL << bit);
        }
        if ((pfds[i].revents & 0x004) != 0) {
            wout[word] |= (1ULL << bit);
        }
        if ((pfds[i].revents & 0x008) != 0) {
            eout[word] |= (1ULL << bit);
        }
    }
    if (rin) {
        rin[0] = rout[0];
        if (nfds > 64) {
            rin[1] = rout[1];
        }
    }
    if (win) {
        win[0] = wout[0];
        if (nfds > 64) {
            win[1] = wout[1];
        }
    }
    if (ein) {
        ein[0] = eout[0];
        if (nfds > 64) {
            ein[1] = eout[1];
        }
    }
    return ready;
}

static long sys_linux_select(long nfds, long readfds, long writefds, long exceptfds, long timeout)
{
    typedef struct {
        long tv_sec;
        long tv_usec;
    } bfree_timeval_t;
    long timeout_ms = -1;

    if (timeout != 0) {
        bfree_timeval_t *tv;
        if (!bfree_user_ptr_mapped(timeout)) {
            return -14;
        }
        tv = (bfree_timeval_t *)(uintptr_t)timeout;
        if (tv->tv_sec < 0 || tv->tv_usec < 0) {
            return -22;
        }
        timeout_ms = (long)(tv->tv_sec * 1000L + tv->tv_usec / 1000L);
    }
    return sys_linux_select_ms(nfds, readfds, writefds, exceptfds, timeout_ms);
}

static long sys_linux_pselect6(long nfds, long readfds, long writefds, long exceptfds,
                               long timeout, long sigmask_ptr)
{
    long timeout_ms = -1;

    (void)sigmask_ptr;
    if (timeout != 0) {
        struct timespec *ts;
        if (!bfree_user_ptr_mapped(timeout)) {
            return -14;
        }
        ts = (struct timespec *)(uintptr_t)timeout;
        if (ts->tv_sec < 0 || ts->tv_nsec < 0) {
            return -22;
        }
        timeout_ms = (long)(ts->tv_sec * 1000L + ts->tv_nsec / 1000000L);
    }
    return sys_linux_select_ms(nfds, readfds, writefds, exceptfds, timeout_ms);
}



static int g_guest_flock_holder[BFREE_GUEST_VFILE_SLOTS]; /* 0 free, else owning_fd+1 */

static long sys_linux_flock(long fd, long op)
{
    int resolved;
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    int vidx;
    int exclusive = ((unsigned)op & 2U) != 0U; /* LOCK_EX */
    int unlock = ((unsigned)op & 8U) != 0U;    /* LOCK_UN */
    int nonblock = ((unsigned)op & 4U) != 0U;  /* LOCK_NB */
    int shared = ((unsigned)op & 1U) != 0U;    /* LOCK_SH */

    (void)shared;
    resolved = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd(resolved, &ofd);
    if (!vf) {
        /* Allow flock on any open fd: map to slot 0 soft lock for non-vfiles. */
        return unlock ? 0 : 0;
    }
    vidx = (int)(vf - g_guest_vfiles);
    if (vidx < 0 || vidx >= BFREE_GUEST_VFILE_SLOTS) {
        return -22;
    }
    if (unlock) {
        if (g_guest_flock_holder[vidx] == (int)fd + 1 ||
            g_guest_flock_holder[vidx] == resolved + 1) {
            g_guest_flock_holder[vidx] = 0;
        }
        return 0;
    }
    if (exclusive || shared) {
        /* LOCK_NB → EAGAIN; blocking waits like pipe read (coop/gthr/EINTR). */
        for (;;) {
            if (g_guest_flock_holder[vidx] == 0 ||
                g_guest_flock_holder[vidx] == (int)fd + 1 ||
                g_guest_flock_holder[vidx] == resolved + 1) {
                g_guest_flock_holder[vidx] = (int)fd + 1;
                return 0;
            }
            if (nonblock) {
                return -11; /* EAGAIN */
            }
            if (g_guest_fork_active && g_coop_side == 1) {
                return bfree_coop_yield_to_parent();
            }
            if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
                return bfree_coop_yield_to_child();
            }
            {
                long sw = bfree_gthr_yield();
                if (sw != 0) {
                    return sw;
                }
            }
            {
                int er = bfree_guest_sig_take_eintr();
                if (er < 0) {
                    return er;
                }
            }
            __asm__ volatile("sti; pause; cli" ::: "memory");
        }
    }
    return -22;
}


static uint64_t g_guest_alarm_deadline_us;
static uint64_t g_guest_alarm_interval_us;
static int g_guest_alarm_armed;

static void bfree_guest_alarm_poll(void)
{
    uint64_t now;

    if (!g_guest_alarm_armed) {
        return;
    }
    now = knl_get_current_time();
    if (now >= g_guest_alarm_deadline_us) {
        bfree_guest_sig_raise(14); /* SIGALRM */
        if (g_guest_alarm_interval_us > 0) {
            g_guest_alarm_deadline_us = now + g_guest_alarm_interval_us;
            g_guest_alarm_armed = 1;
        } else {
            g_guest_alarm_armed = 0;
            g_guest_alarm_deadline_us = 0;
        }
    }
}

static long sys_linux_alarm(long sec)
{
    long prev = 0;
    if (g_guest_alarm_armed) {
        uint64_t now = knl_get_current_time();
        if (g_guest_alarm_deadline_us > now) {
            prev = (long)((g_guest_alarm_deadline_us - now) / 1000000ULL);
            if (prev == 0) {
                prev = 1;
            }
        }
    }
    g_guest_alarm_interval_us = 0;
    if (sec <= 0) {
        g_guest_alarm_armed = 0;
        return prev;
    }
    g_guest_alarm_deadline_us = knl_get_current_time() + (uint64_t)sec * 1000000ULL;
    g_guest_alarm_armed = 1;
    return prev;
}

/* Linux itimerval: two timeval {tv_sec, tv_usec} (ITIMER_REAL only). */
typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} bfree_timeval64_t;

typedef struct {
    bfree_timeval64_t it_interval;
    bfree_timeval64_t it_value;
} bfree_itimerval64_t;

static long sys_linux_getitimer(long which, long curr)
{
    bfree_itimerval64_t out;

    if (which != 0) { /* only ITIMER_REAL */
        return -22;
    }
    if (curr != 0 && !bfree_user_range_mapped((uint64_t)(uintptr_t)curr, sizeof(out))) {
        return -14;
    }
    memset(&out, 0, sizeof(out));
    if (g_guest_alarm_interval_us > 0) {
        out.it_interval.tv_sec = (int64_t)(g_guest_alarm_interval_us / 1000000ULL);
        out.it_interval.tv_usec = (int64_t)(g_guest_alarm_interval_us % 1000000ULL);
    }
    if (g_guest_alarm_armed) {
        uint64_t now = knl_get_current_time();
        if (g_guest_alarm_deadline_us > now) {
            uint64_t left = g_guest_alarm_deadline_us - now;
            out.it_value.tv_sec = (int64_t)(left / 1000000ULL);
            out.it_value.tv_usec = (int64_t)(left % 1000000ULL);
        }
    }
    if (curr != 0) {
        *(bfree_itimerval64_t *)(uintptr_t)curr = out;
    }
    return 0;
}

static long sys_linux_setitimer(long which, long newv, long oldv)
{
    bfree_itimerval64_t neu;

    if (which != 0) { /* only ITIMER_REAL */
        return -22;
    }
    if (oldv != 0) {
        long gr = sys_linux_getitimer(which, oldv);
        if (gr < 0) {
            return gr;
        }
    }
    if (newv == 0) {
        return 0;
    }
    if (!bfree_user_range_mapped((uint64_t)(uintptr_t)newv, sizeof(neu))) {
        return -14;
    }
    neu = *(const bfree_itimerval64_t *)(uintptr_t)newv;
    {
        uint64_t us = 0;
        uint64_t ius = 0;

        if (neu.it_value.tv_sec > 0) {
            us += (uint64_t)neu.it_value.tv_sec * 1000000ULL;
        }
        if (neu.it_value.tv_usec > 0) {
            us += (uint64_t)neu.it_value.tv_usec;
        }
        if (neu.it_interval.tv_sec > 0) {
            ius += (uint64_t)neu.it_interval.tv_sec * 1000000ULL;
        }
        if (neu.it_interval.tv_usec > 0) {
            ius += (uint64_t)neu.it_interval.tv_usec;
        }
        g_guest_alarm_interval_us = ius;
        if (us == 0) {
            g_guest_alarm_armed = 0;
            g_guest_alarm_deadline_us = 0;
            return 0;
        }
        g_guest_alarm_deadline_us = knl_get_current_time() + us;
        g_guest_alarm_armed = 1;
        return 0;
    }
}

/* Linux 204: sched_getaffinity — report single online CPU 0. */
static long sys_linux_sched_getaffinity(long pid, long len, long user_mask)
{
    uint64_t mask;

    (void)pid;
    if (len < (long)sizeof(mask) || user_mask == 0) {
        return -22;
    }
    if (!bfree_user_range_mapped((uint64_t)(uintptr_t)user_mask, (size_t)len)) {
        return -14;
    }
    mask = 1ULL; /* CPU0 */
    memset((void *)(uintptr_t)user_mask, 0, (size_t)len);
    *(uint64_t *)(uintptr_t)user_mask = mask;
    return (long)sizeof(mask);
}

/* Linux 203: sched_setaffinity — accept any non-empty mask that includes a bit. */
static long sys_linux_sched_setaffinity(long pid, long len, long user_mask)
{
    size_t i;
    int any = 0;

    (void)pid;
    if (len <= 0 || user_mask == 0) {
        return -22;
    }
    if (!bfree_user_range_mapped((uint64_t)(uintptr_t)user_mask, (size_t)len)) {
        return -14;
    }
    for (i = 0; i < (size_t)len; ++i) {
        if (((const unsigned char *)(uintptr_t)user_mask)[i] != 0) {
            any = 1;
            break;
        }
    }
    if (!any) {
        return -22; /* empty mask */
    }
    return 0;
}

#define BFREE_LINUX_CAP_VERSION3 0x20080522U

static long sys_linux_capget(long hdr_ptr, long data_ptr)
{
    typedef struct {
        uint32_t version;
        int pid;
    } bfree_cap_header_t;
    bfree_cap_header_t *hdr;

    if (hdr_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)hdr_ptr, sizeof(*hdr))) {
        return -14;
    }
    hdr = (bfree_cap_header_t *)(uintptr_t)hdr_ptr;
    hdr->version = BFREE_LINUX_CAP_VERSION3;
    if (data_ptr != 0) {
        /* 2 × {effective,permitted,inheritable} u32 for v3 — fill zeros. */
        if (!bfree_user_buf_mapped((uint64_t)(uintptr_t)data_ptr, 24)) {
            return -14;
        }
        memset((void *)(uintptr_t)data_ptr, 0, 24);
    }
    return 0;
}

static long sys_linux_capset(long hdr_ptr, long data_ptr)
{
    (void)hdr_ptr;
    (void)data_ptr;
    return 0;
}

static long sys_linux_preadv2(long fd, long iov_ptr, long iovcnt, long offset,
                              long flags)
{
    long total = 0;
    long i;
    long pos = offset;

    (void)flags;
    if (offset < 0) {
        return -22;
    }
    if (iovcnt <= 0 || iovcnt > 1024) {
        return -22;
    }
    if (iov_ptr == 0 || !bfree_user_ptr_mapped(iov_ptr)) {
        return -14;
    }
    for (i = 0; i < iovcnt; ++i) {
        bfree_linux_iovec_t vec;
        uint64_t vec_addr = (uint64_t)(uintptr_t)(iov_ptr + i * (long)sizeof(vec));
        long chunk;

        if (!bfree_user_buf_mapped(vec_addr, sizeof(vec))) {
            return -14;
        }
        memcpy(&vec, (const void *)(uintptr_t)vec_addr, sizeof(vec));
        if (vec.iov_len == 0) {
            continue;
        }
        if (!bfree_user_buf_mapped(vec.iov_base, vec.iov_len)) {
            return -14;
        }
        chunk = sys_linux_pread64(fd, (long)vec.iov_base, (long)vec.iov_len, pos);
        if (chunk < 0) {
            return total > 0 ? total : chunk;
        }
        if (chunk == 0) {
            break;
        }
        total += chunk;
        pos += chunk;
        if (chunk < (long)vec.iov_len) {
            break;
        }
    }
    return total;
}

static long sys_linux_pwritev2(long fd, long iov_ptr, long iovcnt, long offset,
                               long flags)
{
    long total = 0;
    long i;
    long pos = offset;

    (void)flags;
    if (offset < 0) {
        return -22;
    }
    if (iovcnt <= 0 || iovcnt > 1024) {
        return -22;
    }
    if (iov_ptr == 0 || !bfree_user_ptr_mapped(iov_ptr)) {
        return -14;
    }
    for (i = 0; i < iovcnt; ++i) {
        bfree_linux_iovec_t vec;
        uint64_t vec_addr = (uint64_t)(uintptr_t)(iov_ptr + i * (long)sizeof(vec));
        long chunk;

        if (!bfree_user_buf_mapped(vec_addr, sizeof(vec))) {
            return -14;
        }
        memcpy(&vec, (const void *)(uintptr_t)vec_addr, sizeof(vec));
        if (vec.iov_len == 0) {
            continue;
        }
        if (!bfree_user_buf_mapped(vec.iov_base, vec.iov_len)) {
            return -14;
        }
        chunk = sys_linux_pwrite64(fd, (long)vec.iov_base, (long)vec.iov_len, pos);
        if (chunk < 0) {
            return total > 0 ? total : chunk;
        }
        total += chunk;
        pos += chunk;
        if (chunk < (long)vec.iov_len) {
            break;
        }
    }
    return total;
}

#ifndef BFREE_ENODATA
#define BFREE_ENODATA 61
#endif

static long sys_linux_fsetxattr(long fd, long name_ptr, long value_ptr,
                                long size, long flags)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    char name[32];
    size_t n;

    (void)flags;
    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95; /* EOPNOTSUPP soft for non-vfile */
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (name[0] == '\0') {
        return -22;
    }
    if (size < 0 || size > (long)sizeof(vf->xattr_value)) {
        return -34; /* ERANGE */
    }
    if (size > 0 &&
        (value_ptr == 0 ||
         !bfree_user_buf_mapped((uint64_t)(uintptr_t)value_ptr, (uint64_t)size))) {
        return -14;
    }
    for (n = 0; n < sizeof(vf->xattr_name); ++n) {
        vf->xattr_name[n] = name[n];
        if (name[n] == '\0') {
            break;
        }
    }
    vf->xattr_name[sizeof(vf->xattr_name) - 1] = '\0';
    if (size > 0) {
        memcpy(vf->xattr_value, (const void *)(uintptr_t)value_ptr, (size_t)size);
    }
    vf->xattr_len = (size_t)size;
    return 0;
}

static long sys_linux_fgetxattr(long fd, long name_ptr, long value_ptr, long size)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    char name[32];
    size_t n;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (vf->xattr_name[0] == '\0' || strcmp(vf->xattr_name, name) != 0) {
        return -BFREE_ENODATA;
    }
    if (size == 0) {
        return (long)vf->xattr_len;
    }
    if (size < (long)vf->xattr_len) {
        return -34;
    }
    if (value_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)value_ptr, vf->xattr_len)) {
        return -14;
    }
    if (vf->xattr_len > 0) {
        memcpy((void *)(uintptr_t)value_ptr, vf->xattr_value, vf->xattr_len);
    }
    return (long)vf->xattr_len;
}

static long sys_linux_flistxattr(long fd, long list_ptr, long size)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    size_t namelen;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (vf->xattr_name[0] == '\0') {
        return 0;
    }
    namelen = 0;
    while (vf->xattr_name[namelen] != '\0' && namelen + 1U < sizeof(vf->xattr_name)) {
        ++namelen;
    }
    namelen += 1; /* including NUL */
    if (size == 0) {
        return (long)namelen;
    }
    if (size < (long)namelen) {
        return -34;
    }
    if (list_ptr == 0 ||
        !bfree_user_buf_mapped((uint64_t)(uintptr_t)list_ptr, namelen)) {
        return -14;
    }
    memcpy((void *)(uintptr_t)list_ptr, vf->xattr_name, namelen);
    return (long)namelen;
}

static long sys_linux_fremovexattr(long fd, long name_ptr)
{
    bfree_guest_ofd_t *ofd = 0;
    bfree_guest_vfile_t *vf;
    char name[32];
    size_t n;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf || vf->is_dir || vf->is_symlink) {
        return -95;
    }
    if (name_ptr == 0 || !bfree_user_ptr_mapped(name_ptr)) {
        return -14;
    }
    for (n = 0; n + 1U < sizeof(name); ++n) {
        char c = ((const char *)(uintptr_t)name_ptr)[n];
        name[n] = c;
        if (c == '\0') {
            break;
        }
    }
    name[n] = '\0';
    if (vf->xattr_name[0] == '\0' || strcmp(vf->xattr_name, name) != 0) {
        return -BFREE_ENODATA;
    }
    vf->xattr_name[0] = '\0';
    vf->xattr_len = 0;
    return 0;
}


static long sys_linux_sigaltstack(long uss, long uoss)
{
    /* Linux x86_64 stack_t: ss_sp, ss_flags, ss_size (with padding). */
    typedef struct {
        uint64_t ss_sp;
        int32_t ss_flags;
        int32_t pad;
        uint64_t ss_size;
    } bfree_stack_t;
    bfree_stack_t cur;
    bfree_stack_t neu;

    cur.ss_sp = (uint64_t)(uintptr_t)g_sigalt_sp;
    cur.ss_size = (uint64_t)g_sigalt_size;
    cur.pad = 0;
    if (g_sigalt_disable || g_sigalt_sp == 0) {
        cur.ss_flags = BFREE_SS_DISABLE;
    } else if (bfree_sigalt_onstack(g_bfree_user_sysret_rsp)) {
        cur.ss_flags = BFREE_SS_ONSTACK;
    } else {
        cur.ss_flags = 0;
    }

    if (uoss != 0) {
        if (!bfree_user_range_mapped((uint64_t)(uintptr_t)uoss, sizeof(bfree_stack_t))) {
            return -14;
        }
        *(bfree_stack_t *)(uintptr_t)uoss = cur;
    }

    if (uss == 0) {
        return 0;
    }
    if (!bfree_user_range_mapped((uint64_t)(uintptr_t)uss, sizeof(bfree_stack_t))) {
        return -14;
    }
    if (!g_sigalt_disable && g_sigalt_sp != 0 &&
        bfree_sigalt_onstack(g_bfree_user_sysret_rsp)) {
        return -1; /* EPERM */
    }
    neu = *(const bfree_stack_t *)(uintptr_t)uss;
    /* Soft: accept SS_AUTODISARM but do not auto-disarm (documented residual). */
    if ((neu.ss_flags & ~(BFREE_SS_ONSTACK | BFREE_SS_DISABLE | (int32_t)BFREE_SS_AUTODISARM)) != 0) {
        return -22; /* EINVAL */
    }
    if ((neu.ss_flags & BFREE_SS_DISABLE) != 0) {
        g_sigalt_sp = 0;
        g_sigalt_size = 0;
        g_sigalt_disable = 1;
        return 0;
    }
    if (neu.ss_size < (uint64_t)BFREE_MINSIGSTKSZ || neu.ss_sp == 0) {
        return -22;
    }
    if (!bfree_user_range_mapped(neu.ss_sp, (size_t)neu.ss_size)) {
        return -14;
    }
    g_sigalt_sp = (void *)(uintptr_t)neu.ss_sp;
    g_sigalt_size = (size_t)neu.ss_size;
    g_sigalt_disable = 0;
    return 0;
}

#ifndef BFREE_RESTORE_SOFT_STUBS
#define BFREE_RESTORE_SOFT_STUBS 1
static long sys_linux_fchown(long fd, long uid, long gid)
{
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd;

    fd = bfree_guest_fd_resolve((int)fd);
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    (void)ofd;
    if (!vf) {
        return 0; /* non-vfile: soft success */
    }
    if (uid != -1) {
        vf->uid = (uint32_t)uid;
    }
    if (gid != -1) {
        vf->gid = (uint32_t)gid;
    }
    return 0;
}
static long sys_linux_chown(long dirfd, long path_ptr, long uid, long gid)
{
    char path[192];
    char vname[64];
    bfree_guest_vfile_t *vf;
    long path_err;

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dirfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    if (strncmp(path, "/dev/shm/", 9) == 0 && path[9] != '\0') {
        char alt[192];
        size_t i;

        alt[0] = '/'; alt[1] = 't'; alt[2] = 'm'; alt[3] = 'p';
        alt[4] = '/'; alt[5] = 's'; alt[6] = 'h'; alt[7] = 'm';
        alt[8] = '/';
        for (i = 0; path[9 + i] != '\0' && i + 10U < sizeof(alt); ++i) {
            alt[9 + i] = path[9 + i];
        }
        alt[9 + i] = '\0';
        for (i = 0; alt[i] != '\0' && i + 1U < sizeof(path); ++i) {
            path[i] = alt[i];
        }
        path[i] = '\0';
    }
    if (!bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) ||
        vname[0] == '\0') {
        return 0; /* outside /tmp: soft success */
    }
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf) {
        return -2; /* ENOENT */
    }
    if (uid != -1) {
        vf->uid = (uint32_t)uid;
    }
    if (gid != -1) {
        vf->gid = (uint32_t)gid;
    }
    return 0;
}
/* sys_linux_flock: real impl below */
static long sys_linux_flock(long fd, long op);
static long sys_linux_fsync(long fd)
{
    int resolved;
    bfree_guest_ofd_t *ofd;
    int target;
    int vidx = -1;

    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved < 0) {
        return 0;
    }
    ofd = bfree_guest_ofd_from_fd(resolved);
    target = ofd ? ofd->target : resolved;
    if (target >= 0 && target < BFREE_GUEST_VFILE_SLOTS && g_guest_vfiles[target].used) {
        vidx = target;
    }
    if (vidx >= 0) {
        bfree_guest_shared_mmap_sync_vfile(vidx);
        bfree_persist_maybe_flush(&g_guest_vfiles[vidx]);
    }
    return 0;
}
/* sys_linux_alarm / getitimer / setitimer: real impls above */
static long sys_linux_alarm(long sec);
static long sys_linux_getitimer(long which, long curr);
static long sys_linux_setitimer(long which, long newv, long oldv);
static long sys_linux_sched_getaffinity(long pid, long len, long user_mask);
static long sys_linux_getsid(long pid) {
    if (pid == 0) return (long)g_guest_sid;
    if (pid == 1 || (g_guest_fork_active && pid == g_guest_fork_pid)) return (long)g_guest_sid;
    return -3; /* ESRCH */
}
static long sys_linux_set_robust_list(long head, long len) { (void)head;(void)len; return 0; }
static long sys_linux_rt_sigpending(long set) { (void)set; return 0; }
/* H01: sys_linux_rt_sigreturn provided above */
static long sys_linux_clock_getres_linux(long clk, long tp) { (void)clk;(void)tp; return 0; }
/* bfree_pty_slot_from_fd: real impl above */
static unsigned initrd_presence_mask(void) { return 0; }
#endif

/* === Practical ABI holes (B2.26): thin fills for musl/BusyBox/Desktop === */

static long sys_linux_pause(void)
{
    for (;;) {
        if ((g_guest_sig_pending & ~g_guest_sig_mask) != 0ULL) {
            return -4; /* EINTR — delivery via bfree_guest_sig_try_deliver */
        }
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static long sys_linux_time(long tloc)
{
    uint64_t us = knl_get_current_time();
    long sec = (long)(us / 1000000ULL);

    if (tloc != 0) {
        if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)tloc)) {
            return -14;
        }
        *(long *)(uintptr_t)tloc = sec;
    }
    return sec;
}

static long sys_linux_setreuid(long ruid, long euid)
{
    if (ruid != -1) {
        g_guest_uid = (uint32_t)ruid;
    }
    if (euid != -1) {
        g_guest_euid = (uint32_t)euid;
    }
    return 0;
}

static long sys_linux_setregid(long rgid, long egid)
{
    if (rgid != -1) {
        g_guest_gid = (uint32_t)rgid;
    }
    if (egid != -1) {
        g_guest_egid = (uint32_t)egid;
    }
    return 0;
}

static long sys_linux_setresuid(long ruid, long euid, long suid)
{
    (void)suid;
    return sys_linux_setreuid(ruid, euid);
}

static long sys_linux_setresgid(long rgid, long egid, long sgid)
{
    (void)sgid;
    return sys_linux_setregid(rgid, egid);
}

static long sys_linux_getresuid(long ruid, long euid, long suid)
{
    if (ruid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ruid)) {
        *(uint32_t *)(uintptr_t)ruid = g_guest_uid;
    } else if (ruid != 0) {
        return -14;
    }
    if (euid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)euid)) {
        *(uint32_t *)(uintptr_t)euid = g_guest_euid;
    } else if (euid != 0) {
        return -14;
    }
    if (suid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)suid)) {
        *(uint32_t *)(uintptr_t)suid = g_guest_uid;
    } else if (suid != 0) {
        return -14;
    }
    return 0;
}

static long sys_linux_getresgid(long rgid, long egid, long sgid)
{
    if (rgid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)rgid)) {
        *(uint32_t *)(uintptr_t)rgid = g_guest_gid;
    } else if (rgid != 0) {
        return -14;
    }
    if (egid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)egid)) {
        *(uint32_t *)(uintptr_t)egid = g_guest_egid;
    } else if (egid != 0) {
        return -14;
    }
    if (sgid != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)sgid)) {
        *(uint32_t *)(uintptr_t)sgid = g_guest_gid;
    } else if (sgid != 0) {
        return -14;
    }
    return 0;
}

static long sys_linux_rt_sigpending_real(long set, long sigsetsize)
{
    if (sigsetsize < (long)sizeof(uint64_t)) {
        return -22;
    }
    if (set == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)set)) {
        return -14;
    }
    *(uint64_t *)(uintptr_t)set = g_guest_sig_pending;
    return 0;
}

static long sys_linux_rt_sigtimedwait(long uset, long uinfo, long uts, long sigsetsize)
{
    typedef struct {
        long tv_sec;
        long tv_nsec;
    } bfree_ts_t;
    uint64_t want;
    uint64_t deadline_us = 0;
    int finite = 0;
    int sig;

    if (sigsetsize < (long)sizeof(uint64_t)) {
        return -22;
    }
    if (uset == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)uset)) {
        return -14;
    }
    want = *(const uint64_t *)(uintptr_t)uset;

    if (uts != 0) {
        bfree_ts_t ts;

        if (!bfree_user_range_mapped((uint64_t)(uintptr_t)uts, sizeof(ts))) {
            return -14;
        }
        ts = *(const bfree_ts_t *)(uintptr_t)uts;
        if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
            sig = bfree_gthr_sigwait_pick(want, uinfo);
            return sig > 0 ? (long)sig : -11; /* EAGAIN */
        }
        finite = 1;
        deadline_us = knl_get_current_time()
            + ((uint64_t)ts.tv_sec * 1000000ULL)
            + ((uint64_t)ts.tv_nsec / 1000ULL);
    }

    sig = bfree_gthr_sigwait_pick(want, uinfo);
    if (sig > 0) {
        return (long)sig;
    }

    /* Coop: park so another guest thread can raise() into our want set. */
    if (bfree_gthr_mt()) {
        long sw = bfree_gthr_park_sigwait(want, finite ? deadline_us : 0ULL, uinfo);

        if (sw != 0) {
            return sw;
        }
    }

    for (;;) {
        sig = bfree_gthr_sigwait_pick(want, uinfo);
        if (sig > 0) {
            return (long)sig;
        }
        if (finite && knl_get_current_time() >= deadline_us) {
            return -110; /* ETIMEDOUT */
        }
        if (bfree_gthr_mt()) {
            long sw = bfree_gthr_park_sigwait(want, finite ? deadline_us : 0ULL, uinfo);

            if (sw != 0) {
                return sw;
            }
        }
        __asm__ volatile("sti; pause" ::: "memory");
    }
}

static long sys_linux_rt_sigqueueinfo(long pid, long sig, long uinfo)
{
    (void)uinfo;
    return sys_linux_kill(pid, sig);
}

static long sys_linux_tgkill(long tgid, long tid, long sig)
{
    (void)tid;
    return sys_linux_kill(tgid, sig);
}

static long sys_linux_tkill(long tid, long sig)
{
    return sys_linux_kill(tid, sig);
}

static long sys_linux_rt_tgsigqueueinfo(long tgid, long tid, long sig, long uinfo)
{
    (void)tid;
    (void)uinfo;
    return sys_linux_kill(tgid, sig);
}

static long sys_linux_getdents(long fd, long dirp, long count)
{
    long r;

    g_getdents_legacy = 1;
    r = sys_linux_getdents64(fd, dirp, count);
    g_getdents_legacy = 0;
    return r;
}

static long sys_linux_creat(long path_ptr, long mode)
{
    long fd = sys_linux_openat(
        BFREE_LINUX_AT_FDCWD, path_ptr,
        (long)(BFREE_LINUX_O_CREAT | 1 /* O_WRONLY */ | BFREE_LINUX_O_TRUNC),
        mode);
    return fd;
}

static long sys_linux_mknodat(long dirfd, long path_ptr, long mode, long dev)
{
    unsigned type = (unsigned)mode & 0170000U;

    (void)dev;
    if (type == 0U || type == 0100000U) { /* REG */
        long fd = sys_linux_openat(
            dirfd, path_ptr,
            (long)(BFREE_LINUX_O_CREAT | 1 | BFREE_LINUX_O_TRUNC), mode);
        if (fd < 0) {
            return fd;
        }
        (void)sys_linux_close(fd);
        return 0;
    }
    if (type == 0040000U) { /* DIR */
        return sys_linux_mkdir(dirfd, path_ptr, mode);
    }
    if (type == 0010000U) { /* FIFO */
        long fd;
        bfree_guest_vfile_t *vf;
        bfree_guest_ofd_t *ofd;

        fd = sys_linux_openat(
            dirfd, path_ptr,
            (long)(BFREE_LINUX_O_CREAT | 1 | BFREE_LINUX_O_TRUNC), mode);
        if (fd < 0) {
            return fd;
        }
        vf = bfree_guest_vfile_from_open_fd(
            bfree_guest_fd_resolve((int)fd), &ofd);
        (void)ofd;
        if (vf) {
            vf->mode = BFREE_LINUX_S_IFIFO | ((uint32_t)mode & 0777U);
        }
        (void)sys_linux_close(fd);
        return 0;
    }
    return -22;
}

/* fallocate() mode bits */
#define BFREE_FALLOC_FL_KEEP_SIZE  0x01
#define BFREE_FALLOC_FL_PUNCH_HOLE 0x02
#define BFREE_FALLOC_FL_ZERO_RANGE 0x10

static long sys_linux_fallocate(long fd, long mode, long offset, long len)
{
    bfree_guest_vfile_t *vf;
    bfree_guest_ofd_t *ofd = 0;
    int resolved;

    if (offset < 0 || len < 0) {
        return -22;
    }
    if ((mode & BFREE_FALLOC_FL_ZERO_RANGE) != 0) {
        size_t start;
        size_t end;
        size_t i;

        if ((mode & ~(long)(BFREE_FALLOC_FL_KEEP_SIZE |
                            BFREE_FALLOC_FL_ZERO_RANGE)) != 0) {
            return -95; /* EOPNOTSUPP */
        }
        resolved = bfree_guest_fd_resolve((int)fd);
        if (resolved < 0) {
            return -9;
        }
        vf = bfree_guest_vfile_from_open_fd(resolved, &ofd);
        if (!vf) {
            return -9;
        }
        if (vf->is_dir) {
            return -21;
        }
        if ((vf->seals & (uint32_t)BFREE_F_SEAL_WRITE) != 0U) {
            return -1;
        }
        if (len == 0) {
            return 0;
        }
        start = (size_t)offset;
        if (start >= vf->len) {
            return 0;
        }
        end = start + (size_t)len;
        if (end > vf->len) {
            end = vf->len;
        }
        for (i = start; i < end; ++i) {
            vf->data[i] = 0;
        }
        bfree_persist_maybe_flush(vf);
        return 0;
    }
    if ((mode & BFREE_FALLOC_FL_PUNCH_HOLE) != 0) {
        size_t start;
        size_t end;
        size_t i;

        /* Linux requires PUNCH_HOLE to be paired with KEEP_SIZE. */
        if ((mode & BFREE_FALLOC_FL_KEEP_SIZE) == 0) {
            return -22;
        }
        if ((mode & ~(long)(BFREE_FALLOC_FL_KEEP_SIZE |
                            BFREE_FALLOC_FL_PUNCH_HOLE)) != 0) {
            return -95; /* EOPNOTSUPP */
        }
        resolved = bfree_guest_fd_resolve((int)fd);
        if (resolved < 0) {
            return -9;
        }
        vf = bfree_guest_vfile_from_open_fd(resolved, &ofd);
        if (!vf) {
            return -9; /* EBADF: no backing vfile to punch */
        }
        if (vf->is_dir) {
            return -21;
        }
        if ((vf->seals & (uint32_t)BFREE_F_SEAL_WRITE) != 0U) {
            return -1; /* EPERM */
        }
        if (len == 0) {
            return 0;
        }
        start = (size_t)offset;
        if (start >= vf->len) {
            return 0; /* hole entirely past EOF: nothing to zero */
        }
        end = start + (size_t)len;
        if (end > vf->len) {
            end = vf->len;
        }
        for (i = start; i < end; ++i) {
            vf->data[i] = 0;
        }
        bfree_persist_maybe_flush(vf);
        return 0;
    }
    if ((mode & ~(long)BFREE_FALLOC_FL_KEEP_SIZE) != 0) {
        return -95; /* EOPNOTSUPP */
    }
    if ((mode & BFREE_FALLOC_FL_KEEP_SIZE) != 0) {
        /* Reserve space without moving EOF: nothing to do on a RAM store. */
        resolved = bfree_guest_fd_resolve((int)fd);
        if (resolved < 0) {
            return -9;
        }
        if (offset + len > (long)BFREE_GUEST_VFILE_SIZE) {
            return -28; /* ENOSPC */
        }
        return 0;
    }
    /* Extend to offset+len (best-effort). */
    return sys_linux_ftruncate(fd, offset + len);
}

static long sys_linux_sync_file_range(long fd, long offset, long nbytes, long flags)
{
    int resolved;
    /* SYNC_FILE_RANGE_* : WAIT_BEFORE=1, WRITE=2, WAIT_AFTER=4 */
    if (offset < 0 || nbytes < 0) {
        return -22;
    }
    if ((flags & ~7L) != 0) {
        return -22;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved < 0) {
        return -9;
    }
    (void)resolved;
    return sys_linux_fsync(fd);
}

/* Linux 221: fadvise64(fd, offset, len, advice) — posix_fadvise. */
static long sys_linux_fadvise64(long fd, long offset, long len, long advice)
{
    int resolved;

    if (offset < 0 || len < 0) {
        return -22;
    }
    if (advice < 0 || advice > 5) { /* POSIX_FADV_NORMAL..NOREUSE */
        return -22;
    }
    resolved = bfree_guest_fd_resolve((int)fd);
    if (resolved < 0) {
        return -9;
    }
    (void)resolved;
    return 0;
}

static long sys_linux_sendfile(long out_fd, long in_fd, long offset_ptr, long count)
{
    bfree_guest_ofd_t *in_ofd = 0;
    bfree_guest_ofd_t *out_ofd = 0;
    bfree_guest_vfile_t *in_vf;
    bfree_guest_vfile_t *out_vf;
    size_t off = 0;
    size_t n;
    size_t i;
    size_t *opos;

    if (count < 0) {
        return -22;
    }
    if (count == 0) {
        return 0;
    }
    in_fd = bfree_guest_fd_resolve((int)in_fd);
    out_fd = bfree_guest_fd_resolve((int)out_fd);
    in_vf = bfree_guest_vfile_from_open_fd((int)in_fd, &in_ofd);
    if (!in_vf || in_vf->is_dir) {
        return -22;
    }
    if (offset_ptr != 0) {
        if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)offset_ptr)) {
            return -14;
        }
        off = (size_t)*(long *)(uintptr_t)offset_ptr;
    } else if (in_ofd) {
        off = in_ofd->pos;
    }
    if (off >= in_vf->len) {
        return 0;
    }
    n = in_vf->len - off;
    if (n > (size_t)count) {
        n = (size_t)count;
    }
    out_vf = bfree_guest_vfile_from_open_fd((int)out_fd, &out_ofd);
    if (out_vf && !out_vf->is_dir && !out_vf->is_symlink) {
        opos = out_ofd ? &out_ofd->pos : &out_vf->pos;
        if (*opos > out_vf->len) {
            *opos = out_vf->len;
        }
        if (*opos >= BFREE_GUEST_VFILE_SIZE) {
            return -28;
        }
        if (n > BFREE_GUEST_VFILE_SIZE - *opos) {
            n = BFREE_GUEST_VFILE_SIZE - *opos;
        }
        for (i = 0; i < n; ++i) {
            out_vf->data[*opos + i] = in_vf->data[off + i];
        }
        *opos += n;
        if (*opos > out_vf->len) {
            out_vf->len = *opos;
        }
    } else if (out_fd == 1 || out_fd == 2 ||
               out_fd == (long)BFREE_GUEST_DEV_TTY_FD) {
        bfree_guest_console_write(in_vf->data + off, n);
    } else if (bfree_guest_is_pipe_wr((int)out_fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)out_fd);
        size_t room;

        if (!ps) {
            return -9;
        }
        room = sizeof(ps->buf) - ps->len;
        if (n > room) {
            n = room;
        }
        if (n == 0) {
            return -11;
        }
        for (i = 0; i < n; ++i) {
            ps->buf[ps->len + i] = in_vf->data[off + i];
        }
        ps->len += n;
    } else {
        return -22;
    }
    if (offset_ptr != 0) {
        *(long *)(uintptr_t)offset_ptr = (long)(off + n);
    } else if (in_ofd) {
        in_ofd->pos = off + n;
    }
    return (long)n;
}

/* Linux 326: copy_file_range(fd_in, off_in, fd_out, off_out, len, flags). */
static long sys_linux_copy_file_range(long fd_in, long off_in_ptr, long fd_out,
                                      long off_out_ptr, long len, long flags)
{
    bfree_guest_ofd_t *in_ofd = 0;
    bfree_guest_ofd_t *out_ofd = 0;
    bfree_guest_vfile_t *in_vf;
    bfree_guest_vfile_t *out_vf;
    size_t in_off = 0;
    size_t out_off = 0;
    size_t n;
    size_t i;

    (void)flags;
    if (len < 0) {
        return -22;
    }
    if (len == 0) {
        return 0;
    }
    fd_in = bfree_guest_fd_resolve((int)fd_in);
    fd_out = bfree_guest_fd_resolve((int)fd_out);
    in_vf = bfree_guest_vfile_from_open_fd((int)fd_in, &in_ofd);
    out_vf = bfree_guest_vfile_from_open_fd((int)fd_out, &out_ofd);
    if (!in_vf || in_vf->is_dir || in_vf->is_symlink ||
        !out_vf || out_vf->is_dir || out_vf->is_symlink) {
        return -22;
    }
    if (off_in_ptr != 0) {
        if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)off_in_ptr)) {
            return -14;
        }
        if (*(long *)(uintptr_t)off_in_ptr < 0) {
            return -22;
        }
        in_off = (size_t)*(long *)(uintptr_t)off_in_ptr;
    } else if (in_ofd) {
        in_off = in_ofd->pos;
    }
    if (off_out_ptr != 0) {
        if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)off_out_ptr)) {
            return -14;
        }
        if (*(long *)(uintptr_t)off_out_ptr < 0) {
            return -22;
        }
        out_off = (size_t)*(long *)(uintptr_t)off_out_ptr;
    } else if (out_ofd) {
        out_off = out_ofd->pos;
    }
    if (in_off >= in_vf->len) {
        return 0;
    }
    n = in_vf->len - in_off;
    if (n > (size_t)len) {
        n = (size_t)len;
    }
    if (out_off >= BFREE_GUEST_VFILE_SIZE) {
        return -28;
    }
    if (n > BFREE_GUEST_VFILE_SIZE - out_off) {
        n = BFREE_GUEST_VFILE_SIZE - out_off;
    }
    for (i = 0; i < n; ++i) {
        out_vf->data[out_off + i] = in_vf->data[in_off + i];
    }
    if (out_off + n > out_vf->len) {
        out_vf->len = out_off + n;
    }
    if (off_in_ptr != 0) {
        *(long *)(uintptr_t)off_in_ptr = (long)(in_off + n);
    } else if (in_ofd) {
        in_ofd->pos = in_off + n;
    }
    if (off_out_ptr != 0) {
        *(long *)(uintptr_t)off_out_ptr = (long)(out_off + n);
    } else if (out_ofd) {
        out_ofd->pos = out_off + n;
    }
    return (long)n;
}

static long sys_linux_accept4(long sockfd, long addr, long addrlen, long flags)
{
    long fd = sys_linux_accept(sockfd, addr, addrlen);
    int resolved;
    int iidx;
    int uidx;

    if (fd < 0) {
        return fd;
    }
    if ((flags & (long)BFREE_LINUX_O_CLOEXEC) != 0 &&
        fd >= 0 && fd < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_cloexec[fd] = 1;
    }
    if ((flags & (long)BFREE_LINUX_O_NONBLOCK) != 0) {
        resolved = bfree_guest_fd_resolve((int)fd);
        iidx = bfree_inet_from_fd(resolved);
        uidx = bfree_unix_from_fd(resolved);
        if (iidx >= 0) {
            g_inet_socks[iidx].nonblock = 1;
        }
        if (uidx >= 0) {
            g_unix_socks[uidx].nonblock = 1;
        }
        (void)sys_linux_fcntl(fd, 4 /* F_SETFL */, BFREE_LINUX_O_NONBLOCK);
    }
    return fd;
}

static long sys_linux_execveat(long dirfd, long path_ptr, long argv, long envp, long flags)
{
    char path[256];
    size_t i;
    bfree_linux_stat_t st;

#ifndef BFREE_AT_EMPTY_PATH
#define BFREE_AT_EMPTY_PATH 0x1000L
#endif

    /* AT_EMPTY_PATH: empty pathname operates on dirfd (prove resolution). */
    if ((flags & BFREE_AT_EMPTY_PATH) != 0L) {
        if (path_ptr == 0) {
            path[0] = '\0';
        } else if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
            return -14;
        }
        if (path[0] == '\0') {
            if (sys_linux_fstat(dirfd, (long)(uintptr_t)&st) != 0) {
                return -9; /* EBADF */
            }
            if ((st.st_mode & BFREE_LINUX_S_IFDIR) == BFREE_LINUX_S_IFDIR) {
                return -21; /* EISDIR */
            }
            /* Regular/other file: not a valid ELF image — ENOEXEC proves path. */
            (void)argv;
            (void)envp;
            return -8; /* ENOEXEC */
        }
    }

    if (path_ptr == 0) {
        return -14;
    }
    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    if (path[0] != '/') {
        long er = bfree_guest_path_at(dirfd, path, sizeof(path));
        if (er != 0) {
            return er;
        }
        for (i = 0; i + 1U < sizeof(g_execve_kpath_override) && path[i] != '\0'; ++i) {
            g_execve_kpath_override[i] = path[i];
        }
        g_execve_kpath_override[i] = '\0';
    }
    return sys_linux_execve(path_ptr, argv, envp);
}

static long bfree_dispatch_linux_guest_syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    g_coop_cur_nr = (unsigned long)num;
    bfree_guest_trace_sc_num(num);
    /* Sparse cases past the 0..332 jump table — handle before switch. */
    if (num == 319) {
        return sys_linux_memfd_create(arg1, arg2);
    }
    if (num == 77) {
        return sys_linux_ftruncate(arg1, arg2);
    }
    if (num == 76) {
        return sys_linux_truncate(arg1, arg2);
    }
    if (num == 95) {
        return sys_linux_umask(arg1);
    }
    switch (num) {
    case 0:
        return sys_linux_read(arg1, arg2, arg3);
    case 1:
        return sys_linux_write(arg1, arg2, arg3);

    case 17: /* pread64 */
        return sys_linux_pread64(arg1, arg2, arg3, arg4);
    case 18: /* pwrite64 */
        return sys_linux_pwrite64(arg1, arg2, arg3, arg4);
    case 295: /* preadv */
        return sys_linux_preadv2(arg1, arg2, arg3, arg4, 0);
    case 296: /* pwritev */
        return sys_linux_pwritev2(arg1, arg2, arg3, arg4, 0);
    case 327: /* preadv2 — flags in R8 (arg5) */
        return sys_linux_preadv2(arg1, arg2, arg3, arg4, arg5);
    case 328: /* pwritev2 — flags in R8 (arg5) */
        return sys_linux_pwritev2(arg1, arg2, arg3, arg4, arg5);
    case 23: /* select */
        return sys_linux_select(arg1, arg2, arg3, arg4, arg5);
    case 34: /* pause */
        return sys_linux_pause();
    case 40: /* sendfile */
        return sys_linux_sendfile(arg1, arg2, arg3, arg4);
    case 326: /* copy_file_range — flags in R9 (arg6) */
        return sys_linux_copy_file_range(arg1, arg2, arg3, arg4, arg5,
                                         (long)g_bfree_user_syscall_r9);
    case 270: /* pselect6 */
        return sys_linux_pselect6(arg1, arg2, arg3, arg4, arg5, 0);
    case 36: /* getitimer */
        return sys_linux_getitimer(arg1, arg2);
    case 37: /* alarm */
        return sys_linux_alarm(arg1);
    case 38: /* setitimer — musl alarm() uses this */
        return sys_linux_setitimer(arg1, arg2, arg3);
    case 41: /* socket */
        return sys_linux_socket(arg1, arg2, arg3);
    case 42: /* connect */
        return sys_linux_connect(arg1, arg2, arg3);
    case 43: /* accept */
        return sys_linux_accept(arg1, arg2, arg3);
    case 288: /* accept4 */
        return sys_linux_accept4(arg1, arg2, arg3, arg4);
    case 289: /* signalfd4 */
        return sys_linux_signalfd4(arg1, arg2, arg3, arg4);
    case 282: /* signalfd (legacy) */
        return sys_linux_signalfd4(-1, arg2, arg3, 0);
    case 44: /* sendto */
        return sys_linux_sendto(arg1, arg2, arg3, arg4, arg5, 0);
    case 45: /* recvfrom */
        return sys_linux_recvfrom(arg1, arg2, arg3, arg4, arg5, 0);
    case 46: /* sendmsg — single-iovec path via sendto */
        return sys_linux_sendmsg(arg1, arg2, arg3);
    case 47: /* recvmsg — single-iovec path via recvfrom */
        return sys_linux_recvmsg(arg1, arg2, arg3);
    case 307: /* sendmmsg */
        return sys_linux_sendmmsg(arg1, arg2, arg3, arg4);
    case 299: /* recvmmsg */
        return sys_linux_recvmmsg(arg1, arg2, arg3, arg4, arg5);
    case 49: /* bind */
        return sys_linux_bind(arg1, arg2, arg3);
    case 50: /* listen */
        return sys_linux_listen(arg1, arg2);
    case 73: /* flock */
        return sys_linux_flock(arg1, arg2);
    case 74: /* fsync */
        return sys_linux_fsync(arg1);
    case 75: /* fdatasync */
        return sys_linux_fsync(arg1);
    case 162: /* sync */
        return 0;
    case 306: /* syncfs */
        return 0;
    case 149: /* mlock */
    case 150: /* munlock */
    case 151: /* mlockall */
    case 152: /* munlockall */
        return 0;
    case 125: /* capget */
        return sys_linux_capget(arg1, arg2);
    case 126: /* capset */
        return sys_linux_capset(arg1, arg2);
    case 122: /* setfsuid */
    case 123: /* setfsgid */
        return arg1; /* Linux returns previous fsuid/fsgid; soft echo */
    case 325: /* mlock2 */
        return 0;
    case 272: /* unshare */
        return 0;
    case 105: /* setuid */
        return sys_linux_setuid(arg1);
    case 106: /* setgid */
        return sys_linux_setgid(arg1);
    case 4:
        return sys_linux_stat(arg1, arg2);
    case 5:
        return sys_linux_fstat(arg1, arg2);
    case 6:
        return sys_linux_lstat(arg1, arg2);
    case 7:
        return sys_linux_poll(arg1, arg2, arg3);
    case 271:
        return sys_linux_ppoll(arg1, arg2, arg3, arg4);
    case 39:
        return sys_linux_getpid();
    case 62: /* kill */
        return sys_linux_kill(arg1, arg2);
    case 137: /* statfs */
        return sys_linux_statfs(arg1, arg2);
    case 138: /* fstatfs */
        return sys_linux_fstatfs(arg1, arg2);
    case 110:
        return sys_linux_getppid();
    case 109: /* setpgid — H06 sync process + guest pgid */
        return sys_linux_setpgid(arg1, arg2);
    case 111: /* getpgrp == getpgid(0) */
        return sys_linux_getpgid(0);
    case 121: /* getpgid */
        return sys_linux_getpgid(arg1);
    case 124: /* getsid */
        return sys_linux_getsid(arg1);
    case 131: /* sigaltstack */
        return sys_linux_sigaltstack(arg1, arg2);
    case 112: /* setsid */
        return sys_linux_setsid();
    case 186:
        return sys_linux_gettid();
    case 302:
        return sys_linux_prlimit64(arg1, arg2, arg3, arg4);
    case 232:
        return sys_linux_epoll_wait(arg1, arg2, arg3, arg4);
    case 233:
        return sys_linux_epoll_ctl(arg1, arg2, arg3, arg4);
    case 281: /* epoll_pwait — timeout is arg4; arg5 is sigmask (ignored) */
        return sys_linux_epoll_wait(arg1, arg2, arg3, arg4);
    case 53:
        return sys_linux_socketpair(arg1, arg2, arg3, arg4);
    case 48: /* shutdown — soft-0 (pipe-backed; no half-close track) */
        return sys_linux_shutdown(arg1, arg2);
    case 51: /* getsockname */
        return sys_linux_getsockname(arg1, arg2, arg3);
    case 52: /* getpeername */
        return sys_linux_getpeername(arg1, arg2, arg3);
    case 54: /* setsockopt — soft-0 */
        return sys_linux_setsockopt(arg1, arg2, arg3, arg4, arg5);
    case 55: /* getsockopt — soft-0 */
        return sys_linux_getsockopt(arg1, arg2, arg3, arg4, arg5);
    case 284:
        return sys_linux_eventfd2(arg1, arg2);
    case 290: /* musl __NR_eventfd2 (284 is __NR_eventfd v1) */
        return sys_linux_eventfd2(arg1, arg2);
    case 283: /* timerfd_create */
        {
            long tfd = sys_timerfd_create(arg1, arg2);
            return tfd < 0 ? -24 : tfd;
        }
    case 286: /* timerfd_settime */
        {
            /* Linux x86_64 itimerspec: 2× {i64 sec, i64 nsec}. */
            typedef struct {
                int64_t sec;
                int64_t nsec;
            } bfree_ts64_t;
            typedef struct {
                bfree_ts64_t it_interval;
                bfree_ts64_t it_value;
            } bfree_its64_t;
            bfree_its64_t raw;
            struct itimerspec neu;
            struct itimerspec old;
            long rc;
            size_t i;
            const uint8_t *src;
            uint8_t *dst;

            if (arg3 == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)arg3, sizeof(raw))) {
                return -14;
            }
            src = (const uint8_t *)(uintptr_t)arg3;
            dst = (uint8_t *)&raw;
            for (i = 0; i < sizeof(raw); ++i) {
                dst[i] = src[i];
            }
            /* musl layout is it_interval then it_value — match Linux. */
            neu.it_interval.tv_sec = (time_t)raw.it_interval.sec;
            neu.it_interval.tv_nsec = (long)raw.it_interval.nsec;
            neu.it_value.tv_sec = (time_t)raw.it_value.sec;
            neu.it_value.tv_nsec = (long)raw.it_value.nsec;
            rc = bfree_timerfd_schedule(bfree_find_timerfd((int)arg1), (int)arg2, &neu,
                                        arg4 != 0 ? &old : 0);
            if (rc < 0) {
                return -22;
            }
            if (arg4 != 0 && bfree_user_ptr_mapped(arg4)) {
                bfree_its64_t out;
                out.it_interval.sec = (int64_t)old.it_interval.tv_sec;
                out.it_interval.nsec = (int64_t)old.it_interval.tv_nsec;
                out.it_value.sec = (int64_t)old.it_value.tv_sec;
                out.it_value.nsec = (int64_t)old.it_value.tv_nsec;
                src = (const uint8_t *)&out;
                dst = (uint8_t *)(uintptr_t)arg4;
                for (i = 0; i < sizeof(out); ++i) {
                    dst[i] = src[i];
                }
            }
            return 0;
        }
    case 287: /* timerfd_gettime */
        {
            typedef struct {
                int64_t sec;
                int64_t nsec;
            } bfree_ts64_t;
            typedef struct {
                bfree_ts64_t it_interval;
                bfree_ts64_t it_value;
            } bfree_its64_t;
            struct itimerspec cur;
            bfree_its64_t out;
            size_t i;
            const uint8_t *src;
            uint8_t *dst;

            if (arg2 == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)arg2, sizeof(out))) {
                return -14;
            }
            if (sys_timerfd_gettime(arg1, (long)(uintptr_t)&cur) < 0) {
                return -22;
            }
            out.it_interval.sec = (int64_t)cur.it_interval.tv_sec;
            out.it_interval.nsec = (int64_t)cur.it_interval.tv_nsec;
            out.it_value.sec = (int64_t)cur.it_value.tv_sec;
            out.it_value.nsec = (int64_t)cur.it_value.tv_nsec;
            src = (const uint8_t *)&out;
            dst = (uint8_t *)(uintptr_t)arg2;
            for (i = 0; i < sizeof(out); ++i) {
                dst[i] = src[i];
            }
            return 0;
        }
    case 291:
        return sys_linux_epoll_create1(arg1);
    case 293:
        return sys_linux_pipe2(arg1, arg2);
    case 2:
        return sys_linux_openat(-100, arg1, arg2, arg3); /* AT_FDCWD + open */
    case 3:
        return sys_linux_close(arg1);
    case 8:
        return sys_linux_lseek(arg1, arg2, arg3);
    case 9:
        return sys_mmap(arg1, arg2, arg3, arg4, arg5);
    case 10:
        return sys_mprotect(arg1, arg2, arg3);
    case 25: /* mremap */
        return sys_linux_mremap(arg1, arg2, arg3, arg4, arg5);
    case 26: /* msync — live MAP_SHARED write-through */
        return sys_linux_msync(arg1, arg2, arg3);
    case 27: /* mincore */
        return sys_linux_mincore(arg1, arg2, arg3);
    case 28:
        return sys_linux_madvise(arg1, arg2, arg3);
    case 221: /* fadvise64 */
        return sys_linux_fadvise64(arg1, arg2, arg3, arg4);
    case 11:
        return sys_munmap(arg1, arg2);
    case 12:
        return sys_brk(arg1);
    case 13:
        return sys_linux_rt_sigaction(arg1, arg2, arg3, arg4);
    case 14:
        return sys_linux_rt_sigprocmask(arg1, arg2, arg3, arg4);
    case 15: /* rt_sigreturn */
        return sys_linux_rt_sigreturn();
    case 16:
        return sys_linux_ioctl(arg1, arg2, arg3);
    case 19: /* readv */
        return sys_linux_readv(arg1, arg2, arg3);
    case 20:
        return sys_linux_writev(arg1, arg2, arg3);
    case 21:
        return sys_linux_access(arg1, arg2);
    case 269: /* faccessat */
        return sys_linux_faccessat(arg1, arg2, arg3, arg4);
    case 439: /* faccessat2 */
        return sys_linux_faccessat(arg1, arg2, arg3, arg4);
    case 22:
        return sys_linux_pipe2(arg1, 0);
    case 32:
        return sys_linux_dup(arg1);
    case 33:
        return sys_linux_dup2(arg1, arg2);
    case 35:
        return sys_linux_nanosleep(arg1, arg2);
    case 130: /* rt_sigsuspend — ash wait uses this after WNOHANG waitpid */
        /* Soft-reap LIVE orphans so the next waitpid can collect them, then
         * return EINTR with SIGCHLD pending to wake ash's waitproc loop. */
        if (bfree_process_live_count() > 0) {
            (void)bfree_process_force_zombie_live();
            g_guest_fork_active = 0;
            g_coop_parent_started = 0;
            g_guest_fork_was_as_copy = 0;
        }
        bfree_guest_sig_raise(17);
        return -4; /* EINTR */
    case 127: /* rt_sigpending */
        return sys_linux_rt_sigpending_real(arg1, arg2);
    case 128: /* rt_sigtimedwait */
        return sys_linux_rt_sigtimedwait(arg1, arg2, arg3, arg4);
    case 129: /* rt_sigqueueinfo */
        return sys_linux_rt_sigqueueinfo(arg1, arg2, arg3);
    case 230: /* clock_nanosleep */
        return sys_linux_clock_nanosleep(arg1, arg2, arg3, arg4);
    case 60:
    case 231:
        /* PR_SET_PDEATHSIG: the parent is going away, so tell the coop child. */
        if (g_guest_pdeathsig > 0 && g_coop_side == 0 && g_guest_fork_active) {
            bfree_guest_sig_raise(g_guest_pdeathsig);
        }
        if (g_guest_thread_active) {
            long te = bfree_guest_thread_exit(arg1);

            /* Non-main thread switch, or still in MT: take the gthr result. */
            if (g_guest_thread_active || te != -1) {
                return te;
            }
            /* Main thread tore down all guest threads — process exit below. */
        }
        if (g_guest_fork_active || bfree_process_child_active()) {
            /* Heal: execve_reset / nested paths may clear the flag while the
             * private-AS child is still live (desktop Terminal→busybox). */
            if (!g_guest_fork_active) {
                g_guest_fork_active = 1;
                uart_puts("[VFORK] exit heal fork_active for child_active\n");
            }
            bfree_guest_fork_child_pipe_close_writers();
            return bfree_guest_exit_from_fork(arg1);
        }
        /* Last-resort: a nofork applet (or ash itself) called _exit. Re-enter
         * busybox instead of parking the only task in an infinite pause.
         *
         * Nested AS-copy (curated fork+wait) clears g_guest_fork_active while the
         * outer vfork+exec child still owns a private AS. Heal CR3 back to the
         * ash parent PT and reload busybox.elf — do not jump to stale curated
         * elf.entry on g_child_page_table (post-suite PF / "ash noise").
         */
        {
            static const char *const k_sh_argv[] = {
                "/busybox.elf", "sh", "-i", 0
            };
            static const char *const k_sh_env[] = {
                "USER=root",
                "HOME=/root",
                "PATH=/bin:/usr/bin:.",
                "PS1=root@bfree:# ",
                0
            };
            bfree_loaded_elf_info_t elf;
            uint64_t user_rsp = 0;
            uint64_t stack_top;
            page_table_t *resume_pt = 0;
            void *entry = 0;
            int ld;

            if (bfree_process_child_active() || bfree_process_live_count() > 0) {
                resume_pt = bfree_process_parent_pt();
                if (!resume_pt && knl_current_task) {
                    resume_pt = (page_table_t *)knl_current_task->page_table_base;
                }
                bfree_process_exit_child((int)arg1);
                if (resume_pt && knl_current_task) {
                    knl_current_task->page_table_base = resume_pt;
                    __asm__ volatile("mov %0, %%cr3" :: "r"(resume_pt) : "memory");
                }
            }

            bfree_guest_stdio_heal_pipes();
            g_guest_fd_target[0] = -1;
            g_guest_fd_target[1] = -1;
            g_guest_fd_target[2] = -1;
            bfree_guest_execve_reset_subsystems(1);
            if (knl_current_task && knl_current_task->page_table_base) {
                bfree_exec_unmap_init_legacy(
                    (page_table_t *)knl_current_task->page_table_base);
                ld = load_elf_image("busybox.elf", &entry,
                                    knl_current_task->page_table_base);
                if (ld == 0 && entry != 0) {
                    bfree_loaded_elf_info_get(&elf);
                    stack_top = knl_current_task->user_stack_top;
                    if (stack_top == 0) {
                        stack_top = BFREE_USER_STACK_TOP_DEFAULT;
                    }
                    if (bfree_user_stack_ensure_pages(
                            stack_top, BFREE_USER_STACK_PAGES_BUSYBOX) == 0 &&
                        bfree_user_exec_prepare_musl_stack_argv(
                            stack_top, 3, k_sh_argv, 4, k_sh_env, &elf,
                            &user_rsp) == 0) {
                        bfree_enable_user_fpu();
                        bfree_user_exec_install_fsbase(user_rsp, 1);
                        g_bfree_sysret_exec_rsp = user_rsp;
                        g_bfree_exec_transfer_rip = (uint64_t)(uintptr_t)entry;
                        g_bfree_sysret_exec_rcx = g_bfree_exec_transfer_rip;
                        g_bfree_sysret_exec_r11 = 0x202ULL;
                        g_bfree_sysret_exec_cr3 = 0;
                        uart_puts("[VFORK] exit_group re-enter busybox\n");
                        return BFREE_SYSRET_EXEC_TRANSFER;
                    }
                }
            }
            /* Fallback: stale curated entry (pre-fix behavior) if reload fails. */
            bfree_loaded_elf_info_get(&elf);
            if (elf.valid && elf.entry != 0 && knl_current_task) {
                stack_top = knl_current_task->user_stack_top;
                if (stack_top == 0) {
                    stack_top = BFREE_USER_STACK_TOP_DEFAULT;
                }
                if (bfree_user_stack_ensure_pages(stack_top,
                        BFREE_USER_STACK_PAGES_BUSYBOX) == 0 &&
                    bfree_user_exec_prepare_musl_stack_argv(stack_top, 3, k_sh_argv,
                        4, k_sh_env, &elf, &user_rsp) == 0) {
                    bfree_enable_user_fpu();
                    bfree_user_exec_install_fsbase(user_rsp, 1);
                    g_bfree_sysret_exec_rsp = user_rsp;
                    g_bfree_exec_transfer_rip = elf.entry;
                    g_bfree_sysret_exec_rcx = g_bfree_exec_transfer_rip;
                    g_bfree_sysret_exec_r11 = 0x202ULL;
                    g_bfree_sysret_exec_cr3 = 0;
                    return BFREE_SYSRET_EXEC_TRANSFER;
                }
            }
        }
        for (;;) {
            __asm__ volatile("pause");
        }
    case 61:
        return sys_linux_waitpid(arg1, arg2, arg3);
    case 247: /* waitid */
        return sys_linux_waitid(arg1, arg2, arg3, arg4);
    case 63:
        return sys_linux_uname(arg1);
    case 72:
        return sys_linux_fcntl(arg1, arg2, arg3);
    case 79:
        return sys_linux_getcwd(arg1, arg2);
    case 80:
        return sys_linux_chdir(arg1);
    case 81: /* fchdir */
        return sys_linux_fchdir(arg1);
    case 83:
        return sys_linux_mkdir(BFREE_LINUX_AT_FDCWD, arg1, arg2);
    case 85: /* creat */
        return sys_linux_creat(arg1, arg2);
    case 78: /* getdents (legacy) */
        return sys_linux_getdents(arg1, arg2, arg3);
    case 82:
        return sys_linux_rename(BFREE_LINUX_AT_FDCWD, arg1, BFREE_LINUX_AT_FDCWD,
                                arg2, 0);
    case 264: /* renameat */
        return sys_linux_rename(arg1, arg2, arg3, arg4, 0);
    case 316: /* renameat2 — flags in arg5 (R8) */
        return sys_linux_rename(arg1, arg2, arg3, arg4, arg5);
    case 87:
        return sys_linux_unlink(BFREE_LINUX_AT_FDCWD, arg1);
    case 263: /* unlinkat: AT_REMOVEDIR => rmdir */
        if ((arg3 & 0x200) != 0) {
            return sys_linux_rmdir(arg1, arg2);
        }
        return sys_linux_unlink(arg1, arg2);
    case 84: /* rmdir */
        return sys_linux_rmdir(BFREE_LINUX_AT_FDCWD, arg1);
    case 258: /* mkdirat */
        return sys_linux_mkdir(arg1, arg2, arg3);
    case 259: /* mknodat */
        return sys_linux_mknodat(arg1, arg2, arg3, arg4);
    case 133: /* mknod — musl mkfifo() uses this, not mknodat */
        return sys_linux_mknodat(BFREE_LINUX_AT_FDCWD, arg1, arg2, arg3);
    case 261: /* futimesat — thin alias of utimensat */
        return sys_linux_utimensat(arg1, arg2, arg3, 0);
    case 277: /* sync_file_range */
        return sys_linux_sync_file_range(arg1, arg2, arg3, arg4);
    case 285: /* fallocate */
        return sys_linux_fallocate(arg1, arg2, arg3, arg4);
    case 275: /* splice — flags in R9 (arg6) */
        return sys_linux_splice(arg1, arg2, arg3, arg4, arg5,
                                (long)g_bfree_user_syscall_r9);
    case 276: /* tee */
        return sys_linux_tee(arg1, arg2, arg3, arg4);
    case 278: /* vmsplice */
        return sys_linux_vmsplice(arg1, arg2, arg3, arg4);
    case 86: /* link */
        return sys_linux_link(BFREE_LINUX_AT_FDCWD, arg1, BFREE_LINUX_AT_FDCWD, arg2);
    case 265: /* linkat */
        return sys_linux_link(arg1, arg2, arg3, arg4);
    case 88: /* symlink */
        return sys_linux_symlink(arg1, BFREE_LINUX_AT_FDCWD, arg2);
    case 266: /* symlinkat */
        return sys_linux_symlink(arg1, arg2, arg3);
    case 89:
        return sys_linux_readlink(BFREE_LINUX_AT_FDCWD, arg1, arg2, arg3);
    case 267: /* readlinkat */
        return sys_linux_readlink(arg1, arg2, arg3, arg4);
    case 90: /* chmod */
        return sys_linux_chmod(BFREE_LINUX_AT_FDCWD, arg1, arg2);
    case 91: /* fchmod */
        return sys_linux_fchmod(arg1, arg2);
    case 268: /* fchmodat */
        return sys_linux_chmod(arg1, arg2, arg3);
    case 92: /* chown */
        return sys_linux_chown(BFREE_LINUX_AT_FDCWD, arg1, arg2, arg3);
    case 93: /* fchown */
        return sys_linux_fchown(arg1, arg2, arg3);
    case 94: /* lchown — no symlink follow distinction yet */
        return sys_linux_chown(BFREE_LINUX_AT_FDCWD, arg1, arg2, arg3);
    case 260: /* fchownat */
        return sys_linux_chown(arg1, arg2, arg3, arg4);
    case 96:
        return sys_gettimeofday(arg1, arg2);
    case 97:
        return sys_getrlimit(arg1, arg2);
    case 98: /* getrusage — soft-0 fill */
        {
            uint8_t *dst;
            size_t i;
            if (arg2 == 0 || !bfree_user_ptr_mapped(arg2)) {
                return -14;
            }
            dst = (uint8_t *)(uintptr_t)arg2;
            for (i = 0; i < 144; ++i) { /* sizeof(struct rusage) on x86_64 musl ≈ 144 */
                dst[i] = 0;
            }
            return 0;
        }
    case 100: /* times */
        {
            typedef struct {
                long tms_utime;
                long tms_stime;
                long tms_cutime;
                long tms_cstime;
            } bfree_tms_t;
            bfree_tms_t *t;
            if (arg1 != 0) {
                if (!bfree_user_ptr_mapped(arg1)) {
                    return -14;
                }
                t = (bfree_tms_t *)(uintptr_t)arg1;
                t->tms_utime = 0;
                t->tms_stime = 0;
                t->tms_cutime = 0;
                t->tms_cstime = 0;
            }
            return (long)(knl_get_current_time() / 10000ULL); /* rough ticks */
        }
    case 99: /* sysinfo */
        return sys_linux_sysinfo(arg1);
    case 140: /* getpriority */
        (void)arg1;
        (void)arg2;
        return 0; /* nice 0 */
    case 141: /* setpriority */
        (void)arg1;
        (void)arg2;
        (void)arg3;
        return 0;
    case 160: /* setrlimit */
        {
            typedef struct {
                uint64_t rlim_cur;
                uint64_t rlim_max;
            } bfree_rlimit64_t;
            bfree_rlimit64_t neu;

            if (arg2 == 0) {
                return -14;
            }
            if (!bfree_user_ptr_mapped(arg2)) {
                return -14;
            }
            neu = *(const bfree_rlimit64_t *)(uintptr_t)arg2;
            if (neu.rlim_cur > neu.rlim_max) {
                return -22;
            }
            bfree_rlimit_store(arg1, neu.rlim_cur, neu.rlim_max);
            return 0;
        }
    case 102:
        return sys_linux_getuid();
    case 104:
        return sys_linux_getgid();
    case 107:
        return sys_linux_geteuid();
    case 108:
        return sys_linux_getegid();
    case 113: /* setreuid */
        return sys_linux_setreuid(arg1, arg2);
    case 114: /* setregid */
        return sys_linux_setregid(arg1, arg2);
    case 117: /* setresuid */
        return sys_linux_setresuid(arg1, arg2, arg3);
    case 118: /* getresuid (x86_64) */
        return sys_linux_getresuid(arg1, arg2, arg3);
    case 119: /* setresgid (x86_64) */
        return sys_linux_setresgid(arg1, arg2, arg3);
    case 120: /* getresgid */
        return sys_linux_getresgid(arg1, arg2, arg3);
    case 115:
        return sys_linux_getgroups(arg1, arg2);
    case 116: /* setgroups */
        return sys_linux_setgroups(arg1, arg2);
    case 158:
        return sys_arch_prctl(arg1, arg2);
    case 170: /* sethostname (was wrongly gethostname) */
        return sys_linux_sethostname(arg1, arg2);
    case 171: /* setdomainname */
        return sys_linux_setdomainname(arg1, arg2);
    case 157:
        return sys_linux_prctl(arg1, arg2, arg3, arg4, arg5);
    case 200: /* tkill */
        return sys_linux_tkill(arg1, arg2);
    case 201: /* time */
        return sys_linux_time(arg1);
    case 234: /* tgkill */
        return sys_linux_tgkill(arg1, arg2, arg3);
    case 297: /* rt_tgsigqueueinfo */
        return sys_linux_rt_tgsigqueueinfo(arg1, arg2, arg3, arg4);
    case 273: /* set_robust_list */
        return sys_linux_set_robust_list_real(arg1, arg2);
    case 274: /* get_robust_list */
        return sys_linux_get_robust_list(arg1, arg2, arg3);
    case 202:
        return sys_futex(arg1, arg2, arg3, arg4, arg5, 0);
    case 204: /* sched_getaffinity — musl sysconf(_SC_NPROCESSORS_ONLN) */
        return sys_linux_sched_getaffinity(arg1, arg2, arg3);
    case 203: /* sched_setaffinity */
        return sys_linux_sched_setaffinity(arg1, arg2, arg3);
    case 218:
        return sys_set_tid_address(arg1);
    case 228:
        return sys_clock_gettime(arg1, arg2);
    case 229: /* clock_getres */
        if (arg2 != 0 && !bfree_user_ptr_mapped(arg2)) {
            return -14;
        }
        return sys_clock_getres(arg1, arg2);
    case 292: /* dup3 */
        {
            long rc = sys_linux_dup2(arg1, arg2);
            if (rc >= 0 && (arg3 & 0x80000) /* O_CLOEXEC */ &&
                arg2 >= 0 && arg2 < BFREE_GUEST_FD_TABLE_SIZE) {
                g_guest_fd_cloexec[arg2] = 1;
            }
            return rc;
        }
    case 257:
        return sys_linux_openat(arg1, arg2, arg3, arg4);
    case 318:
        return sys_linux_getrandom(arg1, arg2, arg3);
    case 324: /* membarrier */
        return sys_linux_membarrier(arg1, arg2, arg3);
    case 334: /* rseq */
        return sys_linux_rseq(arg1, arg2, arg3, arg4);
    case 262:
        return sys_linux_newfstatat(arg1, arg2, arg3, arg4);
    case 217:
        return sys_linux_getdents64(arg1, arg2, arg3);
    case 332:
        return sys_linux_statx(arg1, arg2, arg3, arg4, arg5);
    case 280: /* utimensat */
        return sys_linux_utimensat(arg1, arg2, arg3, arg4);
    case 235: /* utimes */
        return sys_linux_utimes(arg1, arg2);
    case 56: /* clone — vfork-compatible flags only */
        return sys_linux_clone(arg1, arg2, arg3, arg4, arg5);
    case 57: /* fork — cooperative eager AS copy (H02) */
        return bfree_guest_fork_enter(1);
    case 58: /* vfork — shared AS until exec/exit */
        return bfree_guest_fork_enter(0);
    case 59: /* execve — vfork child into private AS when possible */
        return sys_linux_execve(arg1, arg2, arg3);
    case 322: /* execveat */
        return sys_linux_execveat(arg1, arg2, arg3, arg4, arg5);
    case 165: /* mount */
        return sys_linux_mount(arg1, arg2, arg3, arg4, arg5);
    case 166: /* umount2 */
        return sys_linux_umount2(arg1, arg2);
    case 155: /* pivot_root */
        return sys_linux_pivot_root(arg1, arg2);
    case 161: /* chroot */
        return sys_linux_chroot(arg1);
    case 164: /* settimeofday */
        return sys_linux_settimeofday(arg1, arg2);
    case 29: /* shmget */
        return sys_linux_shmget(arg1, arg2, arg3);
    case 30: /* shmat */
        return sys_linux_shmat(arg1, arg2, arg3);
    case 31: /* shmctl */
        return sys_linux_shmctl(arg1, arg2, arg3);
    case 67: /* shmdt */
        return sys_linux_shmdt(arg1);
    case 64: /* semget */
        return sys_linux_semget(arg1, arg2, arg3);
    case 65: /* semop */
        return sys_linux_semop(arg1, arg2, arg3);
    case 66: /* semctl */
        return sys_linux_semctl(arg1, arg2, arg3, arg4);
    case 68: /* msgget */
        return sys_linux_msgget(arg1, arg2);
    case 69: /* msgsnd */
        return sys_linux_msgsnd(arg1, arg2, arg3, arg4);
    case 70: /* msgrcv */
        return sys_linux_msgrcv(arg1, arg2, arg3, arg4, arg5);
    case 71: /* msgctl */
        return sys_linux_msgctl(arg1, arg2, arg3);
    case 159: /* adjtimex */
        return sys_linux_adjtimex(arg1);
    case 179: /* quotactl */
        return sys_linux_quotactl(arg1, arg2, arg3, arg4);
    case 188: /* setxattr */
    case 189: /* lsetxattr */
        return sys_linux_setxattr(arg1, arg2, arg3, arg4, arg5);
    case 191: /* getxattr */
    case 192: /* lgetxattr */
        return sys_linux_getxattr(arg1, arg2, arg3, arg4);
    case 194: /* listxattr */
    case 195: /* llistxattr */
        return sys_linux_listxattr(arg1, arg2, arg3);
    case 197: /* removexattr */
    case 198: /* lremovexattr */
        return sys_linux_removexattr(arg1, arg2);
    case 248: /* add_key */
        return sys_linux_add_key(arg1, arg2, arg3, arg4, arg5);
    case 249: /* request_key */
        return sys_linux_request_key(arg1, arg2, arg3, arg4);
    case 250: /* keyctl */
        return sys_linux_keyctl(arg1, arg2, arg3, arg4, arg5);
    case 298: /* perf_event_open */
        return sys_linux_perf_event_open(arg1, arg2, arg3, arg4, arg5);
    case 305: /* clock_adjtime */
        return sys_linux_clock_adjtime(arg1, arg2);
    case 312: /* kcmp */
        return sys_linux_kcmp(arg1, arg2, arg3, arg4, arg5);
    case 314: /* sched_setattr */
        return sys_linux_sched_setattr(arg1, arg2, arg3);
    case 315: /* sched_getattr */
        return sys_linux_sched_getattr(arg1, arg2, arg3, arg4);
    case 329: /* pkey_mprotect */
        return sys_linux_pkey_mprotect(arg1, arg2, arg3, arg4);
    case 330: /* pkey_alloc */
        return sys_linux_pkey_alloc(arg1, arg2);
    case 331: /* pkey_free */
        return sys_linux_pkey_free(arg1);
    case 428: /* open_tree */
        return sys_linux_open_tree(arg1, arg2, arg3);
    case 429: /* move_mount */
        return sys_linux_move_mount(arg1, arg2, arg3, arg4, arg5);
    case 430: /* fsopen */
        return sys_linux_fsopen(arg1, arg2);
    case 431: /* fsconfig */
        return sys_linux_fsconfig(arg1, arg2, arg3, arg4, arg5);
    case 432: /* fsmount */
        return sys_linux_fsmount(arg1, arg2, arg3);
    case 433: /* fspick */
        return sys_linux_fspick(arg1, arg2, arg3);
    case 425: /* io_uring_setup */
        return sys_linux_io_uring_setup(arg1, arg2);
    case 426: /* io_uring_enter */
        return sys_linux_io_uring_enter(arg1, arg2, arg3, arg4, arg5);
    case 427: /* io_uring_register */
        return sys_linux_io_uring_register(arg1, arg2, arg3, arg4);
    case 442: /* mount_setattr */
        return sys_linux_mount_setattr(arg1, arg2, arg3, arg4, arg5);
    case 444: /* landlock_create_ruleset */
        return sys_linux_landlock_create_ruleset(arg1, arg2, arg3);
    case 445: /* landlock_add_rule */
        return sys_linux_landlock_add_rule(arg1, arg2, arg3, arg4);
    case 446: /* landlock_restrict_self */
        return sys_linux_landlock_restrict_self(arg1, arg2);
    case 101: /* ptrace */
        return sys_linux_ptrace(arg1, arg2, arg3, arg4);
    case 103: /* syslog */
        return sys_linux_syslog(arg1, arg2, arg3);
    case 135: /* personality */
        return sys_linux_personality(arg1);
    case 154: /* modify_ldt */
        return sys_linux_modify_ldt(arg1, arg2, arg3);
    case 163: /* acct */
        return sys_linux_acct(arg1);
    case 167: /* swapon */
        return sys_linux_swapon(arg1, arg2);
    case 168: /* swapoff */
        return sys_linux_swapoff(arg1);
    case 169: /* reboot */
        return sys_linux_reboot(arg1, arg2, arg3, arg4);
    case 175: /* init_module */
        return sys_linux_init_module(arg1, arg2, arg3);
    case 176: /* delete_module */
        return sys_linux_delete_module(arg1, arg2);
    case 313: /* finit_module */
        return sys_linux_finit_module(arg1, arg2, arg3);
    case 317: /* seccomp */
        return sys_linux_seccomp(arg1, arg2, arg3);
    case 321: /* bpf */
        return sys_linux_bpf(arg1, arg2, arg3);
    case 323: /* userfaultfd */
        return sys_linux_userfaultfd(arg1);
    case 440: /* process_madvise */
        return sys_linux_process_madvise(arg1, arg2, arg3, arg4, arg5);
    case 441: /* epoll_pwait2 */
        return sys_linux_epoll_pwait2(arg1, arg2, arg3, arg4, arg5);
    case 449: /* futex_waitv */
        return sys_linux_futex_waitv(arg1, arg2, arg3, arg4, arg5);
    case 451: /* cachestat */
        return sys_linux_cachestat(arg1, arg2, arg3, arg4);
    case 457: /* statmount */
        return sys_linux_statmount(arg1, arg2, arg3, arg4);
    case 458: /* listmount */
        return sys_linux_listmount(arg1, arg2, arg3, arg4);
    case 222: /* timer_create */
        return sys_linux_timer_create(arg1, arg2, arg3);
    case 223: /* timer_settime */
        return sys_linux_timer_settime(arg1, arg2, arg3, arg4);
    case 224: /* timer_gettime */
        return sys_linux_timer_gettime(arg1, arg2);
    case 226: /* timer_delete */
        return sys_linux_timer_delete(arg1);
    case 227: /* clock_settime */
        return sys_linux_clock_settime(arg1, arg2);
    case 251: /* ioprio_set */
        return sys_linux_ioprio_set(arg1, arg2, arg3);
    case 252: /* ioprio_get */
        return sys_linux_ioprio_get(arg1, arg2);
    case 300: /* fanotify_init */
        return sys_linux_fanotify_init(arg1, arg2);
    case 301: /* fanotify_mark */
        return sys_linux_fanotify_mark(arg1, arg2, arg3, arg4, arg5);
    case 303: /* name_to_handle_at */
        return sys_linux_name_to_handle_at(arg1, arg2, arg3, arg4, arg5);
    case 304: /* open_by_handle_at */
        return sys_linux_open_by_handle_at(arg1, arg2, arg3);
    case 308: /* setns */
        return sys_linux_setns(arg1, arg2);
    case 309: /* getcpu */
        return sys_linux_getcpu(arg1, arg2, arg3);
    case 310: /* process_vm_readv */
        return sys_linux_process_vm_readv(arg1, arg2, arg3, arg4, arg5,
                                          (long)g_bfree_user_syscall_r9);
    case 311: /* process_vm_writev */
        return sys_linux_process_vm_writev(arg1, arg2, arg3, arg4, arg5,
                                           (long)g_bfree_user_syscall_r9);
    case 435: /* clone3 */
        return sys_linux_clone3(arg1, arg2);
    case 447: /* memfd_secret */
        return sys_linux_memfd_secret(arg1);
    case 424: /* pidfd_send_signal */
        return sys_linux_pidfd_send_signal(arg1, arg2, arg3, arg4);
    case 434: /* pidfd_open */
        return sys_linux_pidfd_open(arg1, arg2);
    case 438: /* pidfd_getfd */
        return sys_linux_pidfd_getfd(arg1, arg2, arg3);
    case 253: /* inotify_init */
        return sys_linux_inotify_init1(0);
    case 294: /* inotify_init1 */
        return sys_linux_inotify_init1(arg1);
    case 254: /* inotify_add_watch */
        return sys_linux_inotify_add_watch(arg1, arg2, arg3);
    case 255: /* inotify_rm_watch */
        return sys_linux_inotify_rm_watch(arg1, arg2);
    case 190: /* fsetxattr */
        return sys_linux_fsetxattr(arg1, arg2, arg3, arg4, arg5);
    case 193: /* fgetxattr */
        return sys_linux_fgetxattr(arg1, arg2, arg3, arg4);
    case 196: /* flistxattr */
        return sys_linux_flistxattr(arg1, arg2, arg3);
    case 199: /* fremovexattr */
        return sys_linux_fremovexattr(arg1, arg2);
    case 436: /* close_range */
        return sys_linux_close_range(arg1, arg2, arg3);
    case 437: /* openat2 */
        return sys_linux_openat2(arg1, arg2, arg3, arg4);
    default:
        return BFREE_LINUX_SYSCALL_UNHANDLED;
    }
}

static long bfree_dispatch_app_role_syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    /* Do not rewrite %fs.base on every syscall: musl TLS lives in MSR FS_BASE
     * and clobbering it with a stale knl_current_task->user_fsbase breaks fork
     * children (set_tid_address / errno after syscall). */

    if (g_guest_sys_trace > 0) {
        uart_puts("[SYS] nr=");
        uart_puthex64((uint64_t)num);
        uart_puts(" a1=");
        uart_puthex64((uint64_t)arg1);
        uart_puts(" a2=");
        uart_puthex64((uint64_t)arg2);
        uart_puts("\n");
        g_guest_sys_trace--;
    }

    /* B-Free native numbers overlap Linux 0/1/24; disambiguate before Linux dispatch. */
    if (num == 24) {
        if (arg2 > 0 && arg2 <= 4096 && bfree_user_ptr_mapped(arg1)) {
            return sys_debug_serial_write(arg1, arg2);
        }
        return bfree_gthr_yield(); /* Linux sched_yield */
    }
    /* Linux x86_64: 26=msync, 9=mmap. Do not steal 26 for legacy B-Free mmap. */
    if (num == 1001) {
        return sys_get_framebuffer_info(arg1);
    }
    /* Legacy B-Free ABI used nr 0/1 with a *pointer* arg1. Linux musl uses
     * the same nrs as read/write with an fd in arg1 — magic guest fds such as
     * eventfd (0x3600) must NOT be treated as pointers (identity-mapped low VA
     * made bfree_user_ptr_mapped(0x3600) true and stole write(eventfd)). */
    if (num == 0 && arg1 >= 0x100000L && bfree_user_ptr_mapped(arg1) &&
        !bfree_guest_is_eventfd((int)arg1) && !bfree_guest_is_pipe_rd((int)arg1) &&
        !bfree_guest_is_pipe_wr((int)arg1)) {
        return sys_poll_input_event(arg1);
    }
    if (num == 1 && arg1 >= 0x100000L && bfree_user_ptr_mapped(arg1) &&
        !bfree_guest_is_eventfd((int)arg1) && !bfree_guest_is_pipe_rd((int)arg1) &&
        !bfree_guest_is_pipe_wr((int)arg1)) {
        return sys_get_framebuffer_info(arg1);
    }

    {
        long linux_ret = bfree_dispatch_linux_guest_syscall(num, arg1, arg2, arg3, arg4, arg5);
        if (linux_ret != BFREE_LINUX_SYSCALL_UNHANDLED) {
            return bfree_guest_sig_try_deliver(linux_ret);
        }
    }
    bfree_enosys_note(num);
    bfree_audit_log("syscall_deny", "nr", (uint64_t)num);
    return -38;
}

long knl_syscall_handler(long num, long arg1, long arg2, long arg3, long arg4, long arg5) {
    if (g_bfree_post_exec_syscalls > 0) {
        uart_puts("[ELF] post-exec syscall nr=");
        uart_puthex64((uint64_t)(unsigned long)num);
        uart_puts("\n");
        --g_bfree_post_exec_syscalls;
    }
    if (bfree_security_get_role() == BFREE_ROLE_APP) {
        return bfree_dispatch_app_role_syscall(num, arg1, arg2, arg3, arg4, arg5);
    } else if (!bfree_syscall_allowed(num)) {
        bfree_audit_log("syscall_deny", "nr", (uint64_t)num);
        return -1;
    }
    switch (num) {
        case 0: return sys_poll_input_event(arg1);
        case 1: return sys_get_framebuffer_info(arg1);
        case 1001: return sys_get_framebuffer_info(arg1);
        case 2: return sys_clear_screen(arg1);
        case 3: return sys_get_time(arg1);
        case 4: return sys_input_event_pending(arg1);
        case 5: return sys_timerfd_create(arg1, arg2);
        case 6: return sys_timerfd_settime(arg1, arg2, arg3, arg4);
        case 7: return sys_timerfd_gettime(arg1, arg2);
        case 8: return sys_timerfd_read(arg1, arg2);
        case 9: return sys_timerfd_pending(arg1);
        case 10: return sys_timerfd_close(arg1);
        case 11: return sys_signal_setmask(arg1);
        case 12: return sys_signal_pending(arg1);
        case 13: return sys_signal_post(arg1, arg2);
        case 14: return sys_signal_has_ready(arg1);
        case 15: return sys_signal_consume(arg1, arg2);
        case 20: return sys_fbdev_ioctl(arg1, arg2, arg3);
        case 21: return sys_input_ioctl(arg1, arg2, arg3);
        case 22: return sys_ioctl(arg1, arg2, arg3);
        case 23: return sys_get_tk2_snapshot(arg1, arg2);
        case 24: return sys_debug_serial_write(arg1, arg2);

        // --- Wayland IPC/メモリ syscall (25-29) ---
        case 25: return sys_pipe(arg1);
        case 26: return sys_mmap(arg1, arg2, arg3, arg4, arg5);
        case 27: return sys_shm_open(arg1, arg2, arg3);
        case 28: return sys_shm_unlink(arg1);
        case 29: return sys_clock_gettime(arg1, arg2);

        // --- 時刻・タイマー syscall (30-32) ---
        case 30: return sys_clock_getres(arg1, arg2);
        case 31: return sys_nanosleep(arg1, arg2);
        case 32: return sys_clock_nanosleep(arg1, arg2, arg3, arg4);

        // --- その他 syscall (33-40) ---
        case 33: return sys_uname(arg1);
        case 34: return sys_sysconf(arg1);
        case 35: return sys_gethostname(arg1, arg2);
        case 36: return sys_pause();
        case 37: return sys_sched_yield();
        case 38: return sys_isatty(arg1);
        case 39: return sys_tcgetattr(arg1, arg2);
        case 40: return sys_tcsetattr(arg1, arg2, arg3);
        case 41: return sys_exec_initrd(arg1);
        case 42: return sys_legacy_initrd_read(arg1, arg2, arg3, arg4);

        default:
            // printf("[SYSCALL] unknown syscall: %ld\n", num); // カーネルでは標準Cライブラリ不可
            return -1;
    }
}

/* ---- link-fix: definitions restored from snapshots (post-wipe dedup loss) ---- */

/*
 * SF-02: called from knl_timer_tick() (IRQ context, must not switch CR3).
 * Pre-wipe version armed a coop timeslice deadline; that mechanism was not
 * restored (current tree yields at syscall boundaries). Keep a tick counter
 * so the IRQ-side contract stays satisfied.
 */
volatile unsigned long g_bfree_guest_timer_ticks = 0;
/* C: timer observes preempt_count; cooperative resched hint (no CR3 switch in IRQ). */
void bfree_guest_timer_tick_hook(void)
{
    ++g_bfree_guest_timer_ticks;
    if (g_guest_preempt_count == 0 && g_guest_thread_active) {
        g_guest_need_resched = 1;
    }
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static int bfree_unix_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_UNIX_FD_BASE || fd >= (int)BFREE_UNIX_FD_BASE + BFREE_UNIX_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_UNIX_FD_BASE;
    return g_unix_socks[idx].used ? idx : -1;
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static int bfree_inet_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_INET_FD_BASE || fd >= (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_INET_FD_BASE;
    return g_inet_socks[idx].used ? idx : -1;
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static uint16_t bfree_inet_ntohs(uint16_t x)
{
    return (uint16_t)(((x & 0xffU) << 8) | ((x >> 8) & 0xffU));
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static uint32_t bfree_inet_ntohl(uint32_t x)
{
    return ((x & 0xffU) << 24) | ((x & 0xff00U) << 8) |
           ((x >> 8) & 0xff00U) | ((x >> 24) & 0xffU);
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static void bfree_inet_sock_release(int resolved)
{
    int iidx = bfree_inet_from_fd(resolved);
    uint16_t port;

    if (iidx < 0) {
        return;
    }
    port = g_inet_socks[iidx].port;
    if (g_inet_socks[iidx].tcp_pcb >= 0) {
        tcp_min_close(g_inet_socks[iidx].tcp_pcb);
        g_inet_socks[iidx].tcp_pcb = -1;
    }
    if (g_inet_socks[iidx].is_dgram && g_inet_socks[iidx].bound) {
        g_inet_socks[iidx].bound = 0;
        g_inet_socks[iidx].used = 0;
        bfree_inet_udp_unbind_stack(port);
    }
    g_inet_socks[iidx].used = 0;
    g_inet_socks[iidx].listening = 0;
    g_inet_socks[iidx].connected = 0;
    g_inet_socks[iidx].bound = 0;
    g_inet_socks[iidx].accept_rd = -1;
    g_inet_socks[iidx].accept_wr = -1;
    g_inet_socks[iidx].pipe_magic = -1;
    g_inet_socks[iidx].so_error = 0;
    g_inet_socks[iidx].is_v6 = 0;
    bfree_sock_accept_q_reset(g_inet_socks[iidx].q_rd, g_inet_socks[iidx].q_wr,
                              &g_inet_socks[iidx].q_len,
                              &g_inet_socks[iidx].listen_backlog);
}

/* Count pipe-end refs across the live fd table and the inactive coop snap.
 * After AS-copy fork both sides start identical, so live+inactive ⇒ 2x
 * (matches Linux fd duplication). Mutating one side then drops that side only. */
static int bfree_guest_pipe_count_magic(int magic)
{
    int t;
    int refs = 0;
    const int *inactive;
    const int *inactive_dup;

    if (magic < 0) {
        return 0;
    }
    for (t = 0; t < BFREE_GUEST_FD_TABLE_SIZE; ++t) {
        if (g_guest_fd_target[t] == magic || g_guest_fd_dup_save[t] == magic) {
            refs++;
        }
    }
    if (g_guest_fork_active) {
        if (g_coop_side == 0) {
            inactive = g_fd_snap_child;
            inactive_dup = g_fd_dup_save_snap_child;
        } else {
            inactive = g_fd_snap_parent;
            inactive_dup = g_fd_dup_save_snap_parent;
        }
        for (t = 0; t < BFREE_GUEST_FD_TABLE_SIZE; ++t) {
            if (inactive[t] == magic || inactive_dup[t] == magic) {
                refs++;
            }
        }
    }
    return refs;
}

/* restored from syscall.c.pre_replay + H02 coop snap awareness */
static void bfree_guest_pipe_reclaim_dead_slots(void)
{
    int i;
    int t;
    int u;

    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        int rd_refs;
        int wr_refs;
        int rd_magic;
        int wr_magic;

        if (!g_guest_pipes[i].used) {
            continue;
        }
        rd_magic = bfree_guest_pipe_magic_fd(i, 0);
        wr_magic = bfree_guest_pipe_magic_fd(i, 1);
        rd_refs = bfree_guest_pipe_count_magic(rd_magic);
        wr_refs = bfree_guest_pipe_count_magic(wr_magic);
        /* socketpair / connected AF_UNIX keep magics in sock structs, not the fd table. */
        for (u = 0; u < BFREE_UNIX_SLOTS; ++u) {
            int q;

            if (!g_unix_socks[u].used) {
                continue;
            }
            if (g_unix_socks[u].pipe_magic == wr_magic) {
                ++wr_refs;
            } else if (g_unix_socks[u].pipe_magic == rd_magic) {
                ++rd_refs;
            }
            if (g_unix_socks[u].accept_rd == wr_magic) {
                ++wr_refs;
            } else if (g_unix_socks[u].accept_rd == rd_magic) {
                ++rd_refs;
            }
            /* Pending backlog entries hold both ends until accept(). */
            for (q = 0; q < g_unix_socks[u].q_len; ++q) {
                if (g_unix_socks[u].q_rd[q] == rd_magic) {
                    ++rd_refs;
                } else if (g_unix_socks[u].q_rd[q] == wr_magic) {
                    ++wr_refs;
                }
                if (g_unix_socks[u].q_wr[q] == wr_magic) {
                    ++wr_refs;
                } else if (g_unix_socks[u].q_wr[q] == rd_magic) {
                    ++rd_refs;
                }
            }
        }
        for (u = 0; u < BFREE_INET_SLOTS; ++u) {
            int q;

            if (!g_inet_socks[u].used) {
                continue;
            }
            for (q = 0; q < g_inet_socks[u].q_len; ++q) {
                if (g_inet_socks[u].q_rd[q] == rd_magic) {
                    ++rd_refs;
                } else if (g_inet_socks[u].q_rd[q] == wr_magic) {
                    ++wr_refs;
                }
                if (g_inet_socks[u].q_wr[q] == wr_magic) {
                    ++wr_refs;
                } else if (g_inet_socks[u].q_wr[q] == rd_magic) {
                    ++rd_refs;
                }
            }
            if (g_inet_socks[u].pipe_magic < 0) {
                continue;
            }
            if (g_inet_socks[u].pipe_magic == wr_magic) {
                ++wr_refs;
            } else if (g_inet_socks[u].pipe_magic == rd_magic) {
                ++rd_refs;
            }
            if (g_inet_socks[u].accept_rd == wr_magic) {
                ++wr_refs;
            } else if (g_inet_socks[u].accept_rd == rd_magic) {
                ++rd_refs;
            }
        }
        g_guest_pipes[i].rd_open = rd_refs;
        g_guest_pipes[i].wr_open = wr_refs;
        if (rd_refs > 0 || wr_refs > 0) {
            continue;
        }
        /* No live fds: drop unread bytes (pipeline finished) and free slot. */
        for (t = 0; t < BFREE_GUEST_FD_TABLE_SIZE; ++t) {
            if (bfree_guest_pipe_slot_from_magic(g_guest_fd_target[t]) == i) {
                g_guest_fd_target[t] = -1;
            }
            if (bfree_guest_pipe_slot_from_magic(g_guest_fd_dup_save[t]) == i) {
                g_guest_fd_dup_save[t] = -1;
            }
            if (bfree_guest_pipe_slot_from_magic(g_fd_snap_parent[t]) == i) {
                g_fd_snap_parent[t] = -1;
            }
            if (bfree_guest_pipe_slot_from_magic(g_fd_snap_child[t]) == i) {
                g_fd_snap_child[t] = -1;
            }
            if (bfree_guest_pipe_slot_from_magic(g_fd_dup_save_snap_parent[t]) == i) {
                g_fd_dup_save_snap_parent[t] = -1;
            }
            if (bfree_guest_pipe_slot_from_magic(g_fd_dup_save_snap_child[t]) == i) {
                g_fd_dup_save_snap_child[t] = -1;
            }
        }
        g_guest_pipes[i].used = 0;
        g_guest_pipes[i].nonblock = 0;
        g_guest_pipes[i].len = 0;
    }
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static long bfree_guest_shm_open(long name_ptr, long oflag, long mode)
{
    char name[48];
    char vname[64];
    const char *p;
    size_t n = 0;
    int want_create;
    int want_excl;
    int truncate;
    int accmode;
    bfree_guest_vfile_t *vf;
    int target;

    (void)mode; /* no ownership/mode fields in current vfile struct */

    if (copy_user_cstr(name_ptr, name, sizeof(name)) != 0) {
        return -14;
    }
    p = name;
    if (p[0] == '/') {
        ++p;
    }
    if (p[0] == '\0') {
        return -22;
    }
    vname[0] = 's';
    vname[1] = 'h';
    vname[2] = 'm';
    vname[3] = '/';
    while (p[n] != '\0' && n + 5U < sizeof(vname)) {
        vname[4 + n] = p[n];
        ++n;
    }
    if (p[n] != '\0') {
        return -36; /* ENAMETOOLONG */
    }
    vname[4 + n] = '\0';

    want_create = ((unsigned long)oflag & (unsigned long)BFREE_LINUX_O_CREAT) != 0UL;
    want_excl = ((unsigned long)oflag & 0200UL) != 0UL; /* O_EXCL */
    truncate = ((unsigned long)oflag & (unsigned long)BFREE_LINUX_O_TRUNC) != 0UL;
    accmode = (int)((unsigned long)oflag & (unsigned long)BFREE_LINUX_O_ACCMODE);
    (void)accmode;

    vf = bfree_guest_vfile_find_by_name(vname);
    if (vf && want_create && want_excl) {
        return -17; /* EEXIST */
    }
    if (!vf && !want_create) {
        return -2;
    }
    if (!vf) {
        bfree_guest_vfile_t *dir = bfree_guest_vfile_find_by_name("shm");
        if (!dir) {
            int dfd = bfree_guest_vfile_alloc_slot("shm", 1);
            if (dfd < 0) {
                return dfd;
            }
            dir = bfree_guest_vfile_from_fd(dfd);
            if (dir) {
                dir->is_dir = 1;
            }
        }
        target = bfree_guest_vfile_alloc_slot(vname, 1);
        if (target < 0) {
            return target;
        }
        vf = bfree_guest_vfile_from_fd(target);
        if (!vf) {
            return -5;
        }
    } else {
        target = (int)BFREE_GUEST_VFILE_FD_BASE + (int)(vf - g_guest_vfiles);
        if (truncate) {
            vf->len = 0;
            vf->pos = 0;
        }
    }
    return bfree_guest_vfile_publish_open(target, (int)oflag, 0);
}

/* restored from syscall.c.pre_dedup (dedup dropped definition) */
static long bfree_guest_shm_unlink(long name_ptr)
{
    char name[48];
    char vname[64];
    const char *p;
    size_t n = 0;
    bfree_guest_vfile_t *vf;

    if (copy_user_cstr(name_ptr, name, sizeof(name)) != 0) {
        return -14;
    }
    p = name;
    if (p[0] == '/') {
        ++p;
    }
    if (p[0] == '\0') {
        return -22;
    }
    vname[0] = 's';
    vname[1] = 'h';
    vname[2] = 'm';
    vname[3] = '/';
    while (p[n] != '\0' && n + 5U < sizeof(vname)) {
        vname[4 + n] = p[n];
        ++n;
    }
    if (p[n] != '\0') {
        return -36;
    }
    vname[4 + n] = '\0';
    vf = bfree_guest_vfile_find_by_name(vname);
    if (!vf) {
        return -2;
    }
    if (vf->is_dir) {
        return -21; /* EISDIR */
    }
    /* Drop from the namespace immediately; keep storage while OFDs remain. */
    vf->name[0] = '\0';
    if (vf->open_refs > 0) {
        vf->orphaned = 1;
        return 0;
    }
    bfree_guest_vfile_clear_slot(vf);
    return 0;
}
