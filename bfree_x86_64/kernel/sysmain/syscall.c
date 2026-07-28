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


/* H32 AF_INET (restored) */
#ifndef BFREE_LINUX_AF_INET
#define BFREE_LINUX_AF_INET 2
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
    int is_raw;     /* SOCK_RAW (BusyBox ping ICMP) */
    int ip_proto;   /* e.g. IPPROTO_ICMP=1 for raw */
    uint32_t addr;
    uint16_t port;
    int accept_rd;
    int pipe_magic;
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
typedef struct {
    int used;
    int listening;
    int connected;
    int accept_rd;
    int pipe_magic;
    char path[96];
} bfree_unix_sock_t;
static bfree_unix_sock_t g_unix_socks[BFREE_UNIX_SLOTS];
#endif

static int bfree_user_ptr_mapped(long ptr);
static int bfree_user_vaddr_mapped(uint64_t vaddr);
static void bfree_wrmsr64(uint32_t msr, uint64_t val);
static uint64_t bfree_rdmsr64(uint32_t msr);
static int bfree_pty_slot_from_fd(int fd);
static int bfree_inet_from_fd(int fd);
static void bfree_inet_sock_release(int resolved);
static uint16_t bfree_inet_ntohs(uint16_t x);
static uint32_t bfree_inet_ntohl(uint32_t x);
static int bfree_unix_from_fd(int fd);
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
static int bfree_guest_sig_take_eintr(void);
static long bfree_guest_sig_try_deliver(long ret);
static long bfree_guest_exit_from_fork_signal(int sig);
static long sys_linux_poll_common(long fds_ptr, long nfds);
static long bfree_gthr_park_poll(long fds_ptr, long nfds);
static long bfree_gthr_park_futex(volatile int *uaddr, int val);
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
        if (bfree_gthr_poll_ready(&g_gthr[i]) || bfree_gthr_futex_ready(&g_gthr[i])) {
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

static long bfree_gthr_on_eventfd_write(void)
{
    int i;
    int other = -1;
    long sw;

    if (!bfree_gthr_mt()) {
        return 0;
    }
    /* Eventfd write: prefer parked poll waiters over a generic runnable scan. */
    for (i = 0; i < BFREE_GUEST_MAX_THREADS; ++i) {
        if (i == g_gthr_cur || !g_gthr[i].used) {
            continue;
        }
        if (g_gthr[i].state == BFREE_GTHR_WAIT_POLL) {
            other = i;
            break;
        }
    }
    if (other < 0) {
        other = bfree_gthr_find_runnable(g_gthr_cur);
    }
    if (other < 0) {
        return 0;
    }
    if (g_gthr[other].state != BFREE_GTHR_WAIT_POLL &&
        g_gthr[other].state != BFREE_GTHR_RUNNABLE) {
        return 0;
    }
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].rax = (uint64_t)sizeof(uint64_t);
    g_gthr[g_gthr_cur].state = BFREE_GTHR_RUNNABLE;
    /* Mark waiter runnable before publish so we do not depend on a second
     * poll_common walk of the waiter's user pollfd (can EFAULT on edge cases). */
    if (g_gthr[other].state == BFREE_GTHR_WAIT_POLL) {
        g_gthr[other].state = BFREE_GTHR_RUNNABLE;
        g_gthr[other].rax = 1;
        g_gthr[other].poll_fds = 0;
        g_gthr[other].poll_nfds = 0;
    }
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
}

static long bfree_gthr_on_futex_wake(volatile int *uaddr, int want)
{
    int i;
    int woke = 0;
    int other = -1;
    long sw;
    long waker_ret;

    if (!bfree_gthr_mt()) {
        return 0;
    }
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
        if (other < 0) {
            other = i;
        }
        ++woke;
    }
    if (other < 0) {
        return 0;
    }
    waker_ret = woke > 0 ? (long)woke : 1;
    bfree_gthr_save_current();
    g_gthr[g_gthr_cur].rax = (uint64_t)waker_ret;
    g_gthr[g_gthr_cur].state = BFREE_GTHR_RUNNABLE;
    sw = bfree_gthr_publish_switch(other);
    return sw != 0 ? sw : 0;
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
    if (g_guest_fork_active) {
        preempt_enable();
        return -11;
    }
    if (newsp == 0 || !bfree_user_ptr_mapped(newsp)) {
        preempt_enable();
        return -14;
    }
    child_rsp = (uint64_t)(uintptr_t)newsp;
    child_rsp &= ~0xFULL;

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
            (g_coop_parent_in_wait || g_guest_wait_status_ptr != 0) ? 1 : 0;
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
            uart_puts("[VFORK] wait reap wr=");
            uart_puthex64((uint64_t)(unsigned long)wr);
            uart_puts(" st=");
            uart_puthex64((uint64_t)(unsigned)(unsigned int)st);
            uart_puts("\n");
        }

        if (as_copy && g_coop_parent_started) {
            bfree_coop_publish_parent_resume();
            if (parent_waiting) {
                g_coop_parent_resume_rax =
                    (uint64_t)(wr > 0 ? wr : (long)g_guest_fork_pid);
                g_coop_parent_resume_mode = 2;
            }
            bfree_coop_arm_parent_resume();
        } else {
            g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
            g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
            g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
            g_bfree_fork_parent_ret =
                parent_waiting && wr > 0
                    ? (uint64_t)wr
                    : (uint64_t)(long)g_guest_fork_pid;
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
    return BFREE_SYSRET_FORK_PARENT;
}

static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;

    for (;;) {
        rc = bfree_process_wait4(pid,
            (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) ? &status : 0,
            (int)options);
        if (rc > 0) {
            g_guest_fork_status_ready = 0;
            g_coop_parent_in_wait = 0;
            g_guest_wait_status_ptr = 0;
            if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
                *(int *)(uintptr_t)status_ptr = status;
            }
            return rc;
        }
        if (rc < 0) {
            return rc; /* ECHILD */
        }
        if (((unsigned)options & 1U) != 0U) { /* WNOHANG */
            return 0;
        }
        /* H02: live AS-copy child — schedule it instead of sti;hlt forever. */
        if (g_guest_fork_was_as_copy || g_coop_parent_started) {
            if (bfree_process_child_active() || g_guest_fork_active) {
                g_guest_fork_active = 1;
                g_guest_wait_status_ptr = status_ptr;
                g_guest_waitid_active = 0;
                g_coop_parent_in_wait = 1;
                return bfree_coop_yield_to_child();
            }
        }
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked) {
            g_guest_wait_status_ptr = status_ptr;
            g_coop_parent_in_wait = 1;
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
    bfree_siginfo_wait_t *si;

    if (idtype == BFREE_P_PID) {
        pid = id;
    } else if (idtype == BFREE_P_ALL) {
        pid = -1;
    } else {
        return -22; /* EINVAL: P_PGID not supported yet */
    }
    /* Require WEXITED for this stub; ignore WSTOPPED/WCONTINUED. */
    if ((options & BFREE_WEXITED) == 0 && (options & 0x00000002) == 0 &&
        (options & 0x00000008) == 0) {
        /* Some callers pass only WNOHANG; treat as wait for exit. */
    }
    wopts = ((options & BFREE_WNOHANG_ID) != 0) ? 1 : 0;
    rc = bfree_process_wait4(pid, &status, wopts);
    if (rc < 0) {
        return rc;
    }
    if (rc == 0) {
        return 0; /* WNOHANG, nothing ready */
    }
    g_guest_fork_status_ready = 0;
    if (infop != 0 && bfree_user_ptr_mapped(infop)) {
        si = (bfree_siginfo_wait_t *)(uintptr_t)infop;
        si->si_signo = BFREE_SIGCHLD;
        si->si_errno = 0;
        si->si_code = BFREE_CLD_EXITED;
        si->__pad0 = 0;
        si->si_pid = (int)rc;
        si->si_uid = 0;
        si->si_status = (status >> 8) & 0xff;
        si->si_utime = 0;
        si->si_stime = 0;
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

// case 29 / Linux 228: sys_clock_gettime
// CLOCK_MONOTONIC / CLOCK_REALTIME → uptime_us ベースで返す
long sys_clock_gettime(long clockid, long timespec_ptr)
{
    struct timespec *ts = (struct timespec *)timespec_ptr;
    uint64_t us;

    (void)clockid; // CLOCK_MONOTONIC(1) / CLOCK_REALTIME(0) どちらも uptime で代用
    if (ts == 0) {
        return -22;
    }
    if (!bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ts)) {
        return -14;
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
    (void)addr;
    (void)length;
    (void)advice;
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

/* Linux 25: mremap — grow/shrink anonymous guest heap mapping (no move). */
static long sys_linux_mremap(long old_addr, long old_size, long new_size, long flags,
                             long new_addr)
{
    (void)new_addr;
    if (old_size <= 0 || new_size <= 0) {
        return -22;
    }
    /* MREMAP_MAYMOVE unsupported; refuse move requests. */
    if ((flags & 1L) != 0) { /* MREMAP_MAYMOVE */
        return -38; /* ENOSYS — force musl/glibc fallback */
    }
    if (new_size == old_size) {
        return old_addr;
    }
    if (new_size < old_size) {
        (void)sys_munmap(old_addr + new_size, old_size - new_size);
        return old_addr;
    }
    /* Expand in place only when the extension is free — else ENOSYS. */
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

// Linux 200: get_robust_list — musl pthread init; single-threaded guest has none.
static long sys_linux_get_robust_list(long head_ptr, long len_ptr, long pid)
{
    (void)pid;
    if (head_ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)head_ptr)) {
        *(uintptr_t *)(uintptr_t)head_ptr = 0;
    }
    if (len_ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)len_ptr)) {
        *(uintptr_t *)(uintptr_t)len_ptr = 0;
    }
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
        /* Single-thread guest: clear-on-WAIT fallback (Qt workaround). */
        if (!bfree_gthr_mt()) {
            *(int *)(uintptr_t)uaddr = 0;
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

// Linux 97: getrlimit — musl may probe stack limit during startup.
long sys_getrlimit(long resource, long rlim_ptr)
{
    typedef struct {
        uint64_t rlim_cur;
        uint64_t rlim_lim;
    } bfree_rlimit_t;
    bfree_rlimit_t *rlim = (bfree_rlimit_t *)(uintptr_t)rlim_ptr;

    (void)resource;
    if (rlim == 0 || !bfree_user_vaddr_mapped((uint64_t)(uintptr_t)rlim)) {
        return -14;
    }
    rlim->rlim_cur = 16ULL * 1024ULL * 1024ULL;
    rlim->rlim_lim = 16ULL * 1024ULL * 1024ULL;
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
static int g_guest_eventfd_next;

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
#define BFREE_GUEST_VFILE_FD_BASE   0x3710
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
#define BFREE_LINUX_FD_CLOEXEC      1
#define BFREE_LINUX_O_DIRECTORY     0200000


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
    copy_n = vf->len - off;
    if (length > 0 && (size_t)length < copy_n) {
        copy_n = (size_t)length;
    }
    dst = (uint8_t *)(uintptr_t)(uint64_t)mapped;
    for (i = 0; i < copy_n; ++i) {
        dst[i] = vf->data[off + i];
    }
    if (shared && length > 0) {
        vidx = (int)(vf - g_guest_vfiles);
        (void)bfree_guest_shared_mmap_track(vidx, (uint64_t)(uintptr_t)mapped,
                                            (size_t)length, off);
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
    bfree_guest_vfile_t *dir;
    unsigned seq;

    (void)name_ptr;
    (void)flags;
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
    return bfree_guest_vfile_publish_open(target, 2 /* O_RDWR */, 0);
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
static const char g_guest_proc_mounts[] =
    "rootfs / rootfs rw 0 0\n"
    "proc /proc proc rw,relatime 0 0\n"
    "tmpfs /tmp tmpfs rw,relatime 0 0\n"
    "vfile /persist vfile rw,relatime 0 0\n"
    "vfile /home vfile rw,relatime 0 0\n"
    "vfile /var vfile rw,relatime 0 0\n";
static size_t g_guest_proc_mounts_off;
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

static long sys_linux_statfs(long path_ptr, long buf)
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
    char path[256];
    uint8_t *dst;
    size_t i;

    if (buf == 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if (path_ptr != 0 && copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    memset(&st, 0, sizeof(st));
    st.f_type = 0x01021994; /* TMPFS_MAGIC-ish / synthetic */
    st.f_bsize = 4096;
    st.f_blocks = 131072; /* 512MB */
    st.f_bfree = 98304;
    st.f_bavail = 98304;
    st.f_files = 1024;
    st.f_ffree = 1000;
    st.f_namelen = 255;
    st.f_frsize = 4096;
    dst = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < sizeof(st); ++i) {
        dst[i] = ((const uint8_t *)&st)[i];
    }
    return 0;
}

static long sys_linux_fstatfs(long fd, long buf)
{
    (void)fd;
    return sys_linux_statfs(0, buf);
}

static long sys_linux_prlimit64(long pid, long resource, long new_limit, long old_limit)
{
    typedef struct {
        uint64_t rlim_cur;
        uint64_t rlim_max;
    } bfree_rlimit64_t;
    bfree_rlimit64_t soft;
    uint8_t *dst;
    const uint8_t *src;
    size_t i;

    (void)pid;
    (void)resource;
    soft.rlim_cur = 1024;
    soft.rlim_max = 4096;
    if (resource == 3) { /* RLIMIT_STACK */
        soft.rlim_cur = 8ULL * 1024ULL * 1024ULL;
        soft.rlim_max = 16ULL * 1024ULL * 1024ULL;
    } else if (resource == 7) { /* RLIMIT_NOFILE */
        soft.rlim_cur = 1024;
        soft.rlim_max = 4096;
    }
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
        /* Accept and ignore new limits for now. */
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
    {
        int iidx = bfree_inet_from_fd((int)fd);
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
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);
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
        return bfree_guest_read_blob(buf, count, g_guest_proc_mounts,
            sizeof(g_guest_proc_mounts) - 1U, &g_guest_proc_mounts_off);
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
            return -11;
        }
        dst = (uint64_t *)(uintptr_t)buf;
        *dst = g_guest_eventfd_val[idx];
        g_guest_eventfd_val[idx] = 0;
        return (long)sizeof(uint64_t);
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
        for (i = 0; i < n; ++i) {
            vf->data[*posp + i] = src[i];
        }
        *posp += n;
        if (*posp > vf->len) {
            vf->len = *posp;
        }
        bfree_persist_maybe_flush(vf);
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
        if (iidx2 >= 0 && g_inet_socks[iidx2].connected &&
            g_inet_socks[iidx2].pipe_magic >= 0) {
            return sys_linux_write(g_inet_socks[iidx2].pipe_magic, buf, count);
        }
    }
    {
        int uidx = bfree_unix_from_fd((int)fd);
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

static long sys_linux_openat(long dirfd, long path_ptr, long flags, long mode)
{
    char path[192];
    char vname[64];
    int accmode;
    int want_dir;
    int want_create;
    int want_trunc;
    int want_excl;
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
            return bfree_guest_vfile_publish_open(target, (int)flags, initial_pos);
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
    /* Stub implementation: always succeed */
    (void)dirfd;
    (void)pathname_ptr;
    (void)times_ptr;
    (void)flags;
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
                g_unix_socks[uidx].accept_rd = -1;
                g_unix_socks[uidx].pipe_magic = -1;
                g_unix_socks[uidx].path[0] = '\0';
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
            if (g_guest_fd_target[i] < 0 && g_guest_fd_dup_save[i] < 0) {
                g_guest_fd_target[i] = resolved;
                g_guest_fd_dup_save[i] = resolved;
                return i;
            }
        }
        return -24;
    case 1: /* F_GETFD */
        if (fd >= 0 && fd < BFREE_GUEST_FD_TABLE_SIZE && g_guest_fd_cloexec[fd]) {
            return 1; /* FD_CLOEXEC */
        }
        return 0;
    case 2: /* F_SETFD */
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
            int pslot = bfree_guest_pipe_slot_from_magic(resolved);

            if (pslot >= 0 && g_guest_pipes[pslot].used &&
                g_guest_pipes[pslot].nonblock) {
                return (long)BFREE_LINUX_O_NONBLOCK;
            }
        }
        return 0;
    case 4: /* F_SETFL */
        {
            int pslot = bfree_guest_pipe_slot_from_magic(resolved);

            if (pslot >= 0 && g_guest_pipes[pslot].used) {
                g_guest_pipes[pslot].nonblock =
                    ((unsigned long)arg & (unsigned long)BFREE_LINUX_O_NONBLOCK) != 0UL;
            }
        }
        return 0;
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

static long sys_linux_getgroups(long size, long list)
{
    uint32_t *groups;

    /* Single-user guest: root belongs only to gid 0. */
    if (size == 0) {
        return 1;
    }
    if (size < 1) {
        return -22; /* EINVAL */
    }
    if (list == 0 || !bfree_user_ptr_mapped(list)) {
        return -14; /* EFAULT */
    }
    groups = (uint32_t *)(uintptr_t)list;
    groups[0] = 0;
    return 1;
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
#define BFREE_LINUX_TCGETS   0x5401
#define BFREE_LINUX_TCSETS   0x5402
#define BFREE_LINUX_TIOCGWINSZ 0x5413

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

    if (bfree_guest_is_pipe_rd(fd) && request == BFREE_LINUX_FIONREAD) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)fd);

        if (arg == 0 || !bfree_user_ptr_mapped(arg) || !ps) {
            return -14;
        }
        pending = (int *)(uintptr_t)arg;
        *pending = (int)ps->len;
        return 0;
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
            {
                struct {
                    unsigned short ws_row;
                    unsigned short ws_col;
                    unsigned short ws_xpixel;
                    unsigned short ws_ypixel;
                } *ws = (void *)(uintptr_t)arg;
                ws->ws_row = 24;
                ws->ws_col = 80;
                ws->ws_xpixel = 0;
                ws->ws_ypixel = 0;
            }
            return 0;
        }
    }
    {
        int resolved = bfree_guest_fd_resolve((int)fd);
        int slot = bfree_pty_slot_from_fd(resolved);
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
        if (resolved == (int)BFREE_GUEST_DEV_TTY_FD) {
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
        mode = BFREE_LINUX_S_IFDIR | 0755U;
        size = 4096;
    } else if (vf->is_symlink) {
        mode = BFREE_LINUX_S_IFLNK | 0777U;
        size = (int64_t)vf->len;
    } else {
        mode = BFREE_LINUX_S_IFREG | 0644U;
        size = (int64_t)vf->len;
    }
    ret = bfree_linux_stat_fill(statbuf, mode, size);
    if (ret == 0) {
        bfree_linux_stat_t *st = (bfree_linux_stat_t *)(uintptr_t)statbuf;
        st->st_ino = 100ULL + (uint64_t)(vf - g_guest_vfiles);
        st->st_nlink = (vf->nlink > 0) ? (uint64_t)vf->nlink : 1ULL;
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
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_mounts) - 1U));
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
    } *st;
    char path[256];
    bfree_linux_stat_t tmp;
    long ret;
    long path_err;

    (void)flags;
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
    if (path_ptr == 0 || copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    path_err = bfree_guest_path_at(dfd, path, sizeof(path));
    if (path_err != 0) {
        return path_err;
    }
    ret = bfree_linux_stat_for_path(path, (long)(uintptr_t)&tmp);
    if (ret != 0) {
        return ret;
    }
    st->stx_mask = 0x000007ffU; /* STATX_BASIC_STATS */
    st->stx_blksize = (uint32_t)tmp.st_blksize;
    st->stx_nlink = (uint32_t)tmp.st_nlink;
    st->stx_uid = tmp.st_uid;
    st->stx_gid = tmp.st_gid;
    st->stx_mode = (uint16_t)tmp.st_mode;
    st->stx_ino = tmp.st_ino;
    st->stx_size = (uint64_t)tmp.st_size;
    st->stx_blocks = (uint64_t)tmp.st_blocks;
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
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0444U,
            (int64_t)(sizeof(g_guest_proc_mounts) - 1U));
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

static long sys_linux_gethostname(long buf, long len)
{
    const char host[] = "bfree";
    size_t n = sizeof(host) - 1U;
    size_t i;

    if (buf == 0 || len <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    if ((size_t)len <= n) {
        return -12;
    }
    for (i = 0; i < n; ++i) {
        ((char *)(uintptr_t)buf)[i] = host[i];
    }
    ((char *)(uintptr_t)buf)[n] = '\0';
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
        static const char *fields[] = {"B-Free", "bfree", "0.1", "guest", "x86_64"};
        char *dsts[] = {u->sysname, u->nodename, u->release, u->version, u->machine};
        int i;
        for (i = 0; i < 5; ++i) {
            size_t j = 0;
            while (fields[i][j] != '\0' && j < 64U) {
                dsts[i][j] = fields[i][j];
                ++j;
            }
        }
        bfree_copy_cstr(u->domainname, sizeof(u->domainname), "(none)");
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

    fd = bfree_guest_fd_resolve((int)fd);
    {
        int target = bfree_guest_open_target((int)fd, &ofd);

        if (target == (int)BFREE_GUEST_ROOT_DIR_FD || target == (int)BFREE_GUEST_TMP_DIR_FD ||
            target == (int)BFREE_GUEST_BIN_DIR_FD || target == (int)BFREE_GUEST_USR_DIR_FD ||
            target == (int)BFREE_GUEST_VAR_DIR_FD || target == (int)BFREE_GUEST_HOME_DIR_FD ||
            target == (int)BFREE_GUEST_PERSIST_DIR_FD ||
            target == (int)BFREE_GUEST_PROC_DIR_FD ||
            target == (int)BFREE_GUEST_PROC_PID_DIR_FD) {
            return -29; /* ESPIPE */
        }
    }
    vf = bfree_guest_vfile_from_open_fd((int)fd, &ofd);
    if (vf) {
        if (vf->is_dir) {
            return -29;
        }
        offp = ofd ? &ofd->pos : &vf->pos;
        total = vf->len;
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
        offp = &g_guest_proc_mounts_off;
        total = sizeof(g_guest_proc_mounts) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_PID2STAT_FD) {
        offp = &g_guest_proc_pid2_stat_off;
        total = sizeof(g_guest_proc_pid2_stat) - 1U;
    } else if (fd == (long)BFREE_GUEST_PROC_PID2CMDLINE_FD) {
        offp = &g_guest_proc_pid2_cmdline_off;
        total = sizeof(g_guest_proc_pid2_cmdline) - 1U;
    } else {
        return -29; /* ESPIPE */
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
    if ((size_t)next > total) {
        next = (long)total;
    }
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

static long sys_linux_rename(long olddirfd, long old_ptr, long newdirfd, long new_ptr)
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
    // 簡易実装：何もしない（将来的にスケジューラと連携）
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
    return entry->event_id < 0 ? -1 : 0;
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
        return 0;
    }
    *value = entry->expirations;
    entry->expirations = 0;
    return 1;
}
long sys_timerfd_pending(long fd) {
    bfree_timerfd_entry_t *entry = bfree_find_timerfd((int)fd);

#ifdef BFREE_RUNTIME_BUILD
    timer_process_events();
#endif

    if (entry == 0) {
        return -1;
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

    if (!bytes || len == 0) {
        return 0;
    }
    while (off < len) {
        uint64_t va = user_vaddr + off;
        uint64_t phys;
        uint64_t chunk;
        uint64_t page_off;

        if (bfree_user_stack_page_phys(va, &phys) != 0) {
            return -1;
        }
        page_off = va & (PAGE_SIZE - 1ULL);
        chunk = PAGE_SIZE - page_off;
        if (chunk > len - off) {
            chunk = len - off;
        }
        bfree_kernel_phys_io_begin();
        if (bfree_kernel_poke_phys(phys + page_off, bytes + off, chunk) != 0) {
            bfree_kernel_phys_io_end();
            return -1;
        }
        bfree_kernel_phys_io_end();
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

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
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
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);
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

    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);

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

#define BFREE_LINUX_SYSCALL_UNHANDLED ((long)0x7FFFFFFFL)

static int bfree_user_ptr_mapped(long ptr)
{
    return ptr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)ptr);
}

/* --- Qt QEventDispatcherUNIX: pipe2 + epoll (musl calls these syscalls directly) --- */

typedef struct {
    int used;
    int fd;
    uint32_t events;
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
    g_unix_socks[a].pipe_magic = wr0; /* write → peer reads rd0 */
    g_unix_socks[a].accept_rd = rd1;  /* read ← peer writes wr1 */
    g_unix_socks[a].path[0] = '\0';

    g_unix_socks[b].used = 1;
    g_unix_socks[b].listening = 0;
    g_unix_socks[b].connected = 1;
    g_unix_socks[b].pipe_magic = wr1;
    g_unix_socks[b].accept_rd = rd0;
    g_unix_socks[b].path[0] = '\0';

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

    if (g_guest_eventfd_next >= BFREE_MAX_GUEST_EVENTFD) {
        return -24;
    }
    g_guest_eventfd_val[g_guest_eventfd_next] = (count != 0) ? (uint64_t)count : 0ULL;
    magic = (int)BFREE_GUEST_EVENTFD_BASE + g_guest_eventfd_next++;
    pub = bfree_guest_fd_publish(magic);
    if (pub < 0) {
        g_guest_eventfd_next--;
        g_guest_eventfd_val[g_guest_eventfd_next] = 0;
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
            if (g_inet_socks[iidx].accept_rd >= 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_inet_socks[iidx].connected && g_inet_socks[iidx].pipe_magic >= 0) {
            mag = g_inet_socks[iidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
                if (g_guest_pipes[slot].len > 0) {
                    mask |= (uint32_t)EPOLLIN;
                }
            }
        }
        return mask;
    }

    uidx = bfree_unix_from_fd(resolved);
    if (uidx >= 0) {
        if (g_unix_socks[uidx].listening) {
            if (g_unix_socks[uidx].accept_rd >= 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_unix_socks[uidx].connected && g_unix_socks[uidx].pipe_magic >= 0) {
            mag = g_unix_socks[uidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
                if (g_guest_pipes[slot].len > 0) {
                    mask |= (uint32_t)EPOLLIN;
                }
            }
        }
    }
    return mask;
}

/* Soft-0 for apps that ignore sockopt failure; no NIC option store. */
static long sys_linux_setsockopt(long fd, long level, long optname, long optval, long optlen)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0;
}

static long sys_linux_getsockopt(long fd, long level, long optname, long optval, long optlen_ptr)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    if (optlen_ptr != 0 && bfree_user_ptr_mapped(optlen_ptr)) {
        *(int *)(uintptr_t)optlen_ptr = 0;
    }
    return 0;
}

static long sys_linux_shutdown(long fd, long how)
{
    (void)fd;
    (void)how;
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
    port_be = bfree_inet_ntohs(g_inet_socks[idx].port);
    addr_be = bfree_inet_ntohl(g_inet_socks[idx].addr == BFREE_INADDR_ANY
                                   ? BFREE_INADDR_LOOPBACK
                                   : g_inet_socks[idx].addr);
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
            g_guest_epoll[i].used = 1;
            g_guest_epoll[i].fd = (int)BFREE_GUEST_EPOLL_FD_BASE + i;
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
    watch->data.u64 = lev.data;
    return 0;
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
    for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES && ready < (int)maxevents; ++i) {
        bfree_guest_epoll_watch_t *w = &inst->watches[i];
        uint32_t revents = 0;
        bfree_linux_epoll_event_t lev;
        size_t n;

        if (!w->used) {
            continue;
        }
        if (bfree_guest_is_pipe_rd(w->fd) && bfree_guest_pipe_readable(w->fd)) {
            revents = EPOLLIN;
        } else if (bfree_guest_is_eventfd(bfree_guest_fd_resolve(w->fd))
                   && g_guest_eventfd_val[bfree_guest_eventfd_index(
                          bfree_guest_fd_resolve(w->fd))] != 0) {
            revents = EPOLLIN;
        } else if (bfree_find_timerfd(w->fd) && sys_timerfd_pending(w->fd) > 0) {
            revents = EPOLLIN;
        } else {
            revents = bfree_guest_sock_ready_mask(w->fd);
        }
        if ((revents & w->events) != 0) {
            lev.events = revents & w->events;
            lev.data = w->data.u64;
            for (n = 0; n < sizeof(lev); ++n) {
                outb[(size_t)ready * sizeof(lev) + n] = ((const uint8_t *)&lev)[n];
            }
            ++ready;
        }
    }
    if (ready > 0) {
        return ready;
    }
    if (timeout_ms == 0) {
        /*
         * Qt QEventDispatcher uses epoll_wait(0) as a non-blocking probe.
         * Returning EINTR (not 0) breaks its busy-spin without blocking guests.
         */
        __asm__ volatile("sti" ::: "memory");
        return -4; /* EINTR */
    }
    {
        uint64_t deadline_us = 0;
        int finite = 0;

        if (timeout_ms > 0) {
            finite = 1;
            deadline_us = knl_get_current_time() + (uint64_t)timeout_ms * 1000ULL;
        }
        for (;;) {
            ready = 0;
            for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES && ready < (int)maxevents; ++i) {
                bfree_guest_epoll_watch_t *w = &inst->watches[i];
                uint32_t revents = 0;
                bfree_linux_epoll_event_t lev;
                size_t n;

                if (!w->used) {
                    continue;
                }
                if (bfree_guest_is_pipe_rd(w->fd) && bfree_guest_pipe_readable(w->fd)) {
                    revents = EPOLLIN;
                } else if (bfree_guest_is_eventfd(bfree_guest_fd_resolve(w->fd))
                           && g_guest_eventfd_val[bfree_guest_eventfd_index(
                                  bfree_guest_fd_resolve(w->fd))] != 0) {
                    revents = EPOLLIN;
                } else if (bfree_find_timerfd(w->fd) && sys_timerfd_pending(w->fd) > 0) {
                    revents = EPOLLIN;
                } else {
                    revents = bfree_guest_sock_ready_mask(w->fd);
                }
                if ((revents & w->events) != 0) {
                    lev.events = revents & w->events;
                    lev.data = w->data.u64;
                    for (n = 0; n < sizeof(lev); ++n) {
                        outb[(size_t)ready * sizeof(lev) + n] = ((const uint8_t *)&lev)[n];
                    }
                    ++ready;
                }
            }
            if (ready > 0) {
                return ready;
            }
            if (finite && knl_get_current_time() >= deadline_us) {
                return 0;
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

    (void)sigmask_ptr;
    if (timeout_ptr != 0) {
        if (!bfree_user_ptr_mapped(timeout_ptr)) {
            return -14;
        }
        if (ts->tv_sec < 0 || ts->tv_nsec < 0) {
            return -22;
        }
        timeout_ms = (long)(ts->tv_sec * 1000LL + ts->tv_nsec / 1000000LL);
        if (timeout_ms == 0) {
            return sys_linux_poll_common(fds_ptr, nfds);
        }
        finite = 1;
        deadline_us = knl_get_current_time()
            + (uint64_t)ts->tv_sec * 1000000ULL
            + (uint64_t)ts->tv_nsec / 1000ULL;
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

static char g_guest_prctl_name[16] = "bfree";

static long sys_linux_prctl(long option, long arg2, long arg3, long arg4, long arg5)
{
    size_t i;

    (void)arg3;
    (void)arg4;
    (void)arg5;
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

    (void)flags;
    if (buf == 0 || buflen <= 0 || !bfree_user_ptr_mapped(buf)) {
        return -14;
    }
    p = (uint8_t *)(uintptr_t)buf;
    for (i = 0; i < buflen; ++i) {
        p[i] = (uint8_t)(0x5A ^ (uint8_t)i);
    }
    return buflen;
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
            (g_coop_parent_in_wait || g_guest_wait_status_ptr != 0) ? 1 : 0;
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
        }
        if (as_copy && g_coop_parent_started) {
            bfree_coop_publish_parent_resume();
            if (parent_waiting) {
                g_coop_parent_resume_rax =
                    (uint64_t)(wr > 0 ? wr : (long)g_guest_fork_pid);
                g_coop_parent_resume_mode = 2;
            }
            bfree_coop_arm_parent_resume();
        } else {
            g_bfree_fork_parent_ret =
                parent_waiting && wr > 0
                    ? (uint64_t)wr
                    : (uint64_t)(long)g_guest_fork_pid;
        }
    }
    g_coop_parent_started = 0;
    g_coop_parent_in_wait = 0;
    g_guest_wait_status_ptr = 0;
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
static long sys_linux_socket(long domain, long type, long protocol)
{
    int i;
    int stype = (int)(type & 0xFF);
    int cloexec = ((unsigned long)type & (unsigned long)BFREE_LINUX_O_CLOEXEC) != 0UL;
    int pub;

    if (domain == BFREE_LINUX_AF_INET) {
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (!g_inet_socks[i].used) {
                g_inet_socks[i].used = 1;
                g_inet_socks[i].listening = 0;
                g_inet_socks[i].connected = 0;
                g_inet_socks[i].bound = 0;
                g_inet_socks[i].is_dgram = (stype == 2); /* SOCK_DGRAM */
                g_inet_socks[i].is_raw = (stype == BFREE_SOCK_RAW);
                g_inet_socks[i].ip_proto = (int)protocol;
                g_inet_socks[i].addr = BFREE_INADDR_ANY;
                g_inet_socks[i].port = 0;
                g_inet_socks[i].accept_rd = -1;
                g_inet_socks[i].pipe_magic = -1;
                g_inet_socks[i].tcp_pcb = -1;
                g_inet_socks[i].peer_addr = 0;
                g_inet_socks[i].peer_port = 0;
                g_inet_socks[i].dg_head = 0;
                g_inet_socks[i].dg_count = 0;
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
            g_unix_socks[i].accept_rd = -1;
            g_unix_socks[i].pipe_magic = -1;
            g_unix_socks[i].path[0] = '\0';
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
                return -98; /* EADDRINUSE */
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

static long sys_linux_listen(long sockfd, long backlog)
{
    int idx;
    (void)backlog;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (!g_inet_socks[idx].bound) {
            return -22;
        }
        g_inet_socks[idx].listening = 1;
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (g_unix_socks[idx].path[0] == '\0') {
        return -22;
    }
    g_unix_socks[idx].listening = 1;
    return 0;
}

static long sys_linux_connect(long sockfd, long addr, long addrlen)
{
    int idx, li, i, slot = -1, rd, wr;
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
            return -111; /* ECONNREFUSED — no loopback listener */
        }
        if (g_inet_socks[li].accept_rd >= 0) {
            return -11; /* EAGAIN — one pending accept */
        }
        for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
            if (!g_guest_pipes[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return -24;
        }
        g_guest_pipes[slot].used = 1;
        g_guest_pipes[slot].rd_open = 1;
        g_guest_pipes[slot].wr_open = 1;
        g_guest_pipes[slot].len = 0;
        g_guest_pipes[slot].nonblock = 0;
        rd = bfree_guest_pipe_magic_fd(slot, 0);
        wr = bfree_guest_pipe_magic_fd(slot, 1);
        g_inet_socks[idx].connected = 1;
        g_inet_socks[idx].pipe_magic = wr;
        g_inet_socks[idx].addr = in_addr;
        g_inet_socks[idx].port = in_port;
        g_inet_socks[li].accept_rd = rd;
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
    while (n + 2U < (size_t)addrlen && n + 1U < sizeof(path) && raw[2 + n] != 0) {
        path[n] = (char)raw[2 + n];
        ++n;
    }
    path[n] = '\0';
    for (li = 0; li < BFREE_UNIX_SLOTS; ++li) {
        if (g_unix_socks[li].used && g_unix_socks[li].listening &&
            strcmp(g_unix_socks[li].path, path) == 0) {
            break;
        }
    }
    if (li >= BFREE_UNIX_SLOTS) {
        return -111;
    }
    if (g_unix_socks[li].accept_rd >= 0) {
        return -11;
    }
    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (!g_guest_pipes[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -24;
    }
    g_guest_pipes[slot].used = 1;
    g_guest_pipes[slot].rd_open = 1;
    g_guest_pipes[slot].wr_open = 1;
    g_guest_pipes[slot].len = 0;
    g_guest_pipes[slot].nonblock = 0;
    rd = bfree_guest_pipe_magic_fd(slot, 0);
    wr = bfree_guest_pipe_magic_fd(slot, 1);
    g_unix_socks[idx].connected = 1;
    g_unix_socks[idx].pipe_magic = wr;
    g_unix_socks[li].accept_rd = rd;
    return 0;
}

static long sys_linux_accept(long sockfd, long addr, long addrlen)
{
    int idx, rd;
    (void)addr;
    (void)addrlen;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (!g_inet_socks[idx].listening) {
            return -22;
        }
        if (g_inet_socks[idx].accept_rd < 0) {
            return -11;
        }
        rd = g_inet_socks[idx].accept_rd;
        g_inet_socks[idx].accept_rd = -1;
        return bfree_guest_fd_publish(rd);
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (!g_unix_socks[idx].listening) {
        return -22;
    }
    if (g_unix_socks[idx].accept_rd < 0) {
        return -11;
    }
    rd = g_unix_socks[idx].accept_rd;
    g_unix_socks[idx].accept_rd = -1;
    return bfree_guest_fd_publish(rd);
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

static long sys_linux_sendto(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    (void)flags;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].is_raw) {
        uint32_t dst_addr;
        uint16_t dst_port;
        if (addr != 0) {
            long perr = bfree_inet_parse_sockaddr(addr, 16, &dst_addr, &dst_port);
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
            /* Dispatch drops arg6; sockaddr_in is 16 bytes. */
            long perr = bfree_inet_parse_sockaddr(addr, 16, &dst_addr, &dst_port);
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
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_unix_socks[idx].pipe_magic, buf, len);
    }
    return sys_linux_write(fd, buf, len);
}

static long sys_linux_recvfrom(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    int peek = ((unsigned long)flags & 2UL) != 0UL; /* MSG_PEEK */
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].is_raw) {
        return bfree_inet_raw_recvfrom(idx, buf, len, addr);
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].tcp_pcb >= 0) {
        int pcb = g_inet_socks[idx].tcp_pcb;
        int n;
        int spins;

        if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        for (spins = 0; spins < 20000; ++spins) {
            n = tcp_min_recv(pcb, (uint8_t *)(uintptr_t)buf, (size_t)len);
            if (n != -11) {
                return (long)n;
            }
            net_runtime_poll();
        }
        return -11;
    }
    if (idx >= 0 && g_inet_socks[idx].is_dgram) {
        bfree_inet_sock_t *s = &g_inet_socks[idx];
        size_t n;
        int h;
        int spins;

        if (buf == 0 || len < 0 || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        /* F2 RX: drain e1000 longer — hostfwd packets often arrive between calls. */
        for (spins = 0; s->dg_count == 0 && spins < 4096; ++spins) {
            net_runtime_poll();
        }
        if (s->dg_count == 0) {
            return -11; /* EAGAIN — no datagram queued */
        }
        h = s->dg_head;
        n = s->dg_len[h];
        if (n > (size_t)len) {
            n = (size_t)len; /* truncate (UDP semantics) */
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
        return (long)n;
    }
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].pipe_magic >= 0) {
        int mag = g_inet_socks[idx].pipe_magic;
        if (bfree_guest_pipe_is_wr_magic(mag)) {
            mag = mag - 1;
        }
        return peek ? bfree_guest_pipe_peek(mag, buf, len) : sys_linux_read(mag, buf, len);
    }
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        /* Prefer peer read end (socketpair); else same-slot rd (connect client). */
        int mag = g_unix_socks[idx].accept_rd;
        if (mag < 0) {
            mag = g_unix_socks[idx].pipe_magic;
            if (bfree_guest_pipe_is_wr_magic(mag)) {
                mag = mag - 1;
            }
        }
        return peek ? bfree_guest_pipe_peek(mag, buf, len) : sys_linux_read(mag, buf, len);
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

/* 46/47: only the common single-iovec, no-cmsg shape (musl UDP/DNS). */
static long sys_linux_sendmsg(long fd, long msg_ptr, long flags)
{
    const bfree_linux_msghdr_t *mh;
    const bfree_linux_iovec_t *iov;

    if (msg_ptr == 0 || !bfree_user_ptr_mapped(msg_ptr)) {
        return -14;
    }
    mh = (const bfree_linux_msghdr_t *)(uintptr_t)msg_ptr;
    if (mh->msg_iovlen != 1 || mh->msg_iov == 0 ||
        !bfree_user_ptr_mapped((long)mh->msg_iov)) {
        return -38; /* multi-iov remains intentional residual */
    }
    iov = (const bfree_linux_iovec_t *)(uintptr_t)mh->msg_iov;
    return sys_linux_sendto(fd, (long)iov->iov_base, (long)iov->iov_len,
                            flags, (long)mh->msg_name, (long)mh->msg_namelen);
}

static long sys_linux_recvmsg(long fd, long msg_ptr, long flags)
{
    bfree_linux_msghdr_t *mh;
    const bfree_linux_iovec_t *iov;

    if (msg_ptr == 0 || !bfree_user_ptr_mapped(msg_ptr)) {
        return -14;
    }
    mh = (bfree_linux_msghdr_t *)(uintptr_t)msg_ptr;
    if (mh->msg_iovlen != 1 || mh->msg_iov == 0 ||
        !bfree_user_ptr_mapped((long)mh->msg_iov)) {
        return -38;
    }
    iov = (const bfree_linux_iovec_t *)(uintptr_t)mh->msg_iov;
    mh->msg_controllen = 0;
    mh->msg_flags = 0;
    return sys_linux_recvfrom(fd, (long)iov->iov_base, (long)iov->iov_len,
                              flags, (long)mh->msg_name, 0);
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
        if (g_guest_flock_holder[vidx] != 0 &&
            g_guest_flock_holder[vidx] != (int)fd + 1 &&
            g_guest_flock_holder[vidx] != resolved + 1) {
            return nonblock ? -11 : -11; /* EAGAIN */
        }
        g_guest_flock_holder[vidx] = (int)fd + 1;
        return 0;
    }
    return -22;
}


static uint64_t g_guest_alarm_deadline_us;
static int g_guest_alarm_armed;

static void bfree_guest_alarm_poll(void)
{
    if (!g_guest_alarm_armed) {
        return;
    }
    if (knl_get_current_time() >= g_guest_alarm_deadline_us) {
        g_guest_alarm_armed = 0;
        bfree_guest_sig_raise(14); /* SIGALRM */
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
    if (sec <= 0) {
        g_guest_alarm_armed = 0;
        return prev;
    }
    g_guest_alarm_deadline_us = knl_get_current_time() + (uint64_t)sec * 1000000ULL;
    g_guest_alarm_armed = 1;
    return prev;
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
static long sys_linux_fchown(long fd, long uid, long gid) { (void)fd;(void)uid;(void)gid; return 0; }
static long sys_linux_chown(long dirfd, long path, long uid, long gid) { (void)dirfd;(void)path;(void)uid;(void)gid; return 0; }
/* sys_linux_flock: real impl below */
static long sys_linux_flock(long fd, long op);
static long sys_linux_fsync(long fd) { (void)fd; return 0; }
/* sys_linux_alarm: real impl below */
static long sys_linux_alarm(long sec);
static long sys_linux_getitimer(long which, long curr) { (void)which;(void)curr; return 0; }
static long sys_linux_setitimer(long which, long newv, long oldv) { (void)which;(void)newv;(void)oldv; return 0; }
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
    case 23: /* select */
        return sys_linux_select(arg1, arg2, arg3, arg4, arg5);
    case 270: /* pselect6 */
        return sys_linux_pselect6(arg1, arg2, arg3, arg4, arg5, 0);
    case 37: /* alarm */
        return sys_linux_alarm(arg1);
    case 41: /* socket */
        return sys_linux_socket(arg1, arg2, arg3);
    case 42: /* connect */
        return sys_linux_connect(arg1, arg2, arg3);
    case 43: /* accept */
        return sys_linux_accept(arg1, arg2, arg3);
    case 44: /* sendto */
        return sys_linux_sendto(arg1, arg2, arg3, arg4, arg5, 0);
    case 45: /* recvfrom */
        return sys_linux_recvfrom(arg1, arg2, arg3, arg4, arg5, 0);
    case 46: /* sendmsg — single-iovec path via sendto */
        return sys_linux_sendmsg(arg1, arg2, arg3);
    case 47: /* recvmsg — single-iovec path via recvfrom */
        return sys_linux_recvmsg(arg1, arg2, arg3);
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
    case 281:
        return sys_linux_epoll_wait(arg1, arg2, arg3, arg5);
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
            struct itimerspec neu;
            struct itimerspec old;
            long rc;

            if (arg3 == 0 || !bfree_user_ptr_mapped(arg3)) {
                return -14;
            }
            {
                const uint8_t *src = (const uint8_t *)(uintptr_t)arg3;
                uint8_t *dst = (uint8_t *)&neu;
                size_t i;
                for (i = 0; i < sizeof(neu); ++i) {
                    dst[i] = src[i];
                }
            }
            rc = bfree_timerfd_schedule(bfree_find_timerfd((int)arg1), (int)arg2, &neu,
                                        arg4 != 0 ? &old : 0);
            if (rc == 0 && arg4 != 0 && bfree_user_ptr_mapped(arg4)) {
                const uint8_t *src = (const uint8_t *)&old;
                uint8_t *dst = (uint8_t *)(uintptr_t)arg4;
                size_t i;
                for (i = 0; i < sizeof(old); ++i) {
                    dst[i] = src[i];
                }
            }
            return rc < 0 ? -22 : rc;
        }
    case 287: /* timerfd_gettime */
        return sys_timerfd_gettime(arg1, arg2) < 0 ? -22 : 0;
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
    case 26: /* msync — anonymous/vfile: soft success */
        return 0;
    case 27: /* mincore */
        return sys_linux_mincore(arg1, arg2, arg3);
    case 28:
        return sys_linux_madvise(arg1, arg2, arg3);
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
    case 22:
        return sys_linux_pipe2(arg1, 0);
    case 32:
        return sys_linux_dup(arg1);
    case 33:
        return sys_linux_dup2(arg1, arg2);
    case 35:
        return sys_linux_nanosleep(arg1, arg2);
    case 230:
        return sys_linux_nanosleep(arg3, arg4);
    case 60:
    case 231:
        if (g_guest_thread_active) {
            long te = bfree_guest_thread_exit(arg1);

            /* Non-main thread switch, or still in MT: take the gthr result. */
            if (g_guest_thread_active || te != -1) {
                return te;
            }
            /* Main thread tore down all guest threads — process exit below. */
        }
        if (g_guest_fork_active) {
            bfree_guest_fork_child_pipe_close_writers();
            return bfree_guest_exit_from_fork(arg1);
        }
        /* Last-resort: a nofork applet (or ash itself) called _exit. Re-enter
         * busybox instead of parking the only task in an infinite pause. */
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

            bfree_guest_stdio_heal_pipes();
            g_guest_fd_target[0] = -1;
            g_guest_fd_target[1] = -1;
            g_guest_fd_target[2] = -1;
            bfree_guest_execve_reset_subsystems(1);
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
                    knl_current_task->user_fsbase = 0;
                    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);
                    g_bfree_sysret_exec_rsp = user_rsp;
                    g_bfree_sysret_exec_rcx = elf.entry;
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
    case 82:
        return sys_linux_rename(BFREE_LINUX_AT_FDCWD, arg1, BFREE_LINUX_AT_FDCWD, arg2);
    case 264: /* renameat */
        return sys_linux_rename(arg1, arg2, arg3, arg4);
    case 316: /* renameat2 */
        return sys_linux_rename(arg1, arg2, arg3, arg4);
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
    case 90: /* chmod — /tmp vfiles have no mode bits; accept and ignore */
    case 91: /* fchmod */
        return 0;
    case 268: /* fchmodat */
        return 0;
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
    case 160: /* setrlimit — accept, store ignored (prlimit64 preferred by musl) */
        (void)arg1;
        if (arg2 != 0 && !bfree_user_ptr_mapped(arg2)) {
            return -14;
        }
        return 0;
    case 102:
        return sys_linux_getuid();
    case 104:
        return sys_linux_getgid();
    case 107:
        return sys_linux_geteuid();
    case 108:
        return sys_linux_getegid();
    case 115:
        return sys_linux_getgroups(arg1, arg2);
    case 158:
        return sys_arch_prctl(arg1, arg2);
    case 170:
        return sys_linux_gethostname(arg1, arg2);
    case 157:
        return sys_linux_prctl(arg1, arg2, arg3, arg4, arg5);
    case 200:
        return sys_linux_get_robust_list(arg1, arg2, arg3);
    case 202:
        return sys_futex(arg1, arg2, arg3, arg4, arg5, 0);
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
    case 56: /* clone — vfork-compatible flags only */
        return sys_linux_clone(arg1, arg2, arg3, arg4, arg5);
    case 57: /* fork — cooperative eager AS copy (H02) */
        return bfree_guest_fork_enter(1);
    case 58: /* vfork — shared AS until exec/exit */
        return bfree_guest_fork_enter(0);
    case 59: /* execve — vfork child into private AS when possible */
        return sys_linux_execve(arg1, arg2, arg3);
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
        return 0; /* Linux sched_yield */
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
    if (bfree_security_get_role() == BFREE_ROLE_APP) {
        return bfree_dispatch_app_role_syscall(num, arg1, arg2, arg3, arg4, arg5);
    } else if (!bfree_syscall_allowed(num)) {
        bfree_audit_log("syscall_deny", "nr", (uint64_t)num);
        return -1;
    }
    switch (num) {
        /* dup case 0 removed */
        /* dup case 1 removed */
        case 1001: return sys_get_framebuffer_info(arg1);
        /* dup case 2 removed */
        /* dup case 3 removed */
        /* dup case 4 removed */
        /* dup case 5 removed */
        /* dup case 6 removed */
        /* dup case 7 removed */
        /* dup case 8 removed */
        /* dup case 9 removed */
        /* dup case 10 removed */
        /* dup case 11 removed */
        /* dup case 12 removed */
        /* dup case 13 removed */
        /* dup case 14 removed */
        case 15: return sys_signal_consume(arg1, arg2);
        /* dup case 20 removed */
        /* dup case 21 removed */
        /* dup case 22 removed */
        case 23: return sys_get_tk2_snapshot(arg1, arg2);
        case 24: return sys_debug_serial_write(arg1, arg2);

        // --- Wayland IPC/メモリ syscall (25-29) ---
        case 25: return sys_pipe(arg1);
        case 26: return sys_mmap(arg1, arg2, arg3, arg4, arg5);
        case 27: return sys_shm_open(arg1, arg2, arg3);
        /* dup case 28 removed */
        case 29: return sys_clock_gettime(arg1, arg2);

        // --- 時刻・タイマー syscall (30-32) ---
        case 30: return sys_clock_getres(arg1, arg2);
        case 31: return sys_nanosleep(arg1, arg2);
        /* dup case 32 removed */

        // --- その他 syscall (33-40) ---
        /* dup case 33 removed */
        case 34: return sys_sysconf(arg1);
        /* dup case 35 removed */
        case 36: return sys_pause();
        case 37: return sys_sched_yield();
        case 38: return sys_isatty(arg1);
        /* dup case 39 removed */
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
    g_inet_socks[iidx].pipe_magic = -1;
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
        }
        for (u = 0; u < BFREE_INET_SLOTS; ++u) {
            if (!g_inet_socks[u].used || g_inet_socks[u].pipe_magic < 0) {
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
