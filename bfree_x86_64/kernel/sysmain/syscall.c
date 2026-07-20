// syscall.c - SYSCALL/SYSRET経路Cハンドラ（仕様書v2.4準拠）
//
// * System V ABI順（RDI, RSI, RDX, RCX, R8, R9）で受け取る
// * syscall番号で個別APIをdispatch
#include <stdint.h>
#include "../string.h"

#include "../fbdev.h"
#include "../gpu_backend.h"
#include "../include/tk/kernel.h"
#include "timer_manager.h"
#include "security_policy.h"
#include "vmm.h"
#include "elf_loader.h"
#include "process.h"
#include "../../userland/libc/bfree_epoll.h"

extern void bfree_enable_user_fpu(void);

extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern void uart_puthex64(uint64_t val);
extern void *pmm_alloc(void);

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

uint64_t g_bfree_sysret_exec_rsp;
uint64_t g_bfree_sysret_exec_rcx;
uint64_t g_bfree_sysret_exec_r11;
uint64_t g_bfree_sysret_exec_rdi;
uint64_t g_bfree_sysret_exec_rsi;
uint64_t g_bfree_sysret_exec_rdx;
/* Non-zero: syscall_entry.S loads this into CR3 before the new user RSP (vfork child AS). */
uint64_t g_bfree_sysret_exec_cr3;

/* --- post-wipe stubs for advanced syscall_entry.S (filled by hole redo) --- */
uint64_t g_bfree_exec_transfer_rip;
uint64_t g_bfree_sig_saved_rbx;
uint64_t g_bfree_sig_saved_rbp;
uint64_t g_bfree_sig_saved_r12;
uint64_t g_bfree_sig_saved_r13;
uint64_t g_bfree_sig_saved_r14;
uint64_t g_bfree_sig_saved_r15;


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

static int g_guest_fork_active;
static int g_guest_fork_pid;
static int g_guest_fork_status;
static int g_guest_fork_status_ready;
static int g_guest_next_pid = 2;
static uintptr_t g_guest_clear_child_tid;
/* Declared early so cooperative fork can snapshot/restore it. */
static char g_guest_cwd[256] = "/";
/* Kernel-side cwd is not in the shared user address space, so without an
 * explicit snapshot a cooperative child `chdir` would permanently move the
 * parent shell. Save/restore gives fork-like cwd isolation for ash subshells. */
static char g_guest_fork_saved_cwd[256];
static uint64_t g_guest_fork_saved_heap_next;
static uint64_t g_guest_fork_saved_brk;
/* Parent stack snapshot: child returns from forkshell before exec and reuses
 * the shared stack, trashing the frozen parent's frame (jp etc.). */
#define BFREE_VFORK_STACK_SAVE_PAGES 16
#define BFREE_VFORK_STACK_SAVE_BYTES ((uint64_t)BFREE_VFORK_STACK_SAVE_PAGES * PAGE_SIZE)
static uint8_t g_guest_fork_stack_save[BFREE_VFORK_STACK_SAVE_BYTES] __attribute__((aligned(16)));
static uint64_t g_guest_fork_stack_save_base;
static int g_guest_fork_stack_save_valid;

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

void bfree_sysret_exec_globals_init(void)
{
    g_bfree_sysret_exec_rsp = 0;
    g_bfree_sysret_exec_rcx = 0;
    g_bfree_sysret_exec_r11 = 0;
    g_bfree_sysret_exec_rdi = 0;
    g_bfree_sysret_exec_rsi = 0;
    g_bfree_sysret_exec_rdx = 0;
    g_bfree_sysret_exec_cr3 = 0;
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
    g_guest_next_pid = 2;
    bfree_process_init();
}


/* Forward decls: unix/coop bodies live after FD table (H14/H02) */
static void bfree_guest_alarm_poll(void);
static int bfree_guest_sig_take_eintr(void);
static void bfree_coop_fd_snap_init(void);
static long bfree_coop_yield_to_parent(void);
static long bfree_coop_yield_to_child(void);
static int bfree_guest_fd_publish(int target);
static long sys_linux_read(long fd, long buf, long count);
static long sys_linux_write(long fd, long buf, long count);

static long bfree_guest_fork_enter(void)
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
    (void)bfree_guest_vfork_stack_snapshot(g_bfree_fork_saved_rsp);

    rc = bfree_process_vfork_enter(&child_pid);
    if (rc < 0) {
        return rc;
    }
    g_guest_fork_pid = child_pid;
    g_guest_fork_active = 1;
    g_guest_fork_status_ready = 0;
    bfree_coop_fd_snap_init();
    return 0; /* cooperative: continue as child; parent resumes on child exit */
}

static long bfree_guest_exit_from_fork(long status)
{
    int *cleartid;
    size_t i;

    bfree_process_exit_child((int)status);
    g_guest_fork_active = 0;
    g_guest_fork_status = (int)(status & 0xff);
    g_guest_fork_status_ready = 1;
    /* Shared fd table: a pipeline child may leave stdin/stdout wired to a
     * pipe. Restore the shell's console before the parent resumes. */
    bfree_guest_stdio_heal_pipes();
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
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
    g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
    g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
    uart_puts("[VFORK] parent resume rip=");
    uart_puthex64(g_bfree_fork_saved_rcx);
    uart_puts(" rsp=");
    uart_puthex64(g_bfree_fork_saved_rsp);
    uart_puts(" rdx=");
    uart_puthex64(g_bfree_fork_saved_rdx);
    uart_puts(" fs=");
    uart_puthex64(g_guest_fork_saved_fsbase);
    uart_puts("\n");
    return BFREE_SYSRET_FORK_PARENT;
}

static long sys_linux_waitpid(long pid, long status_ptr, long options)
{
    int status = 0;
    long rc;

    rc = bfree_process_wait4(pid,
        (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) ? &status : 0,
        (int)options);
    if (rc > 0) {
        g_guest_fork_status_ready = 0;
        if (status_ptr != 0 && bfree_user_ptr_mapped(status_ptr)) {
            *(int *)(uintptr_t)status_ptr = status;
        }
    }
    return rc;
}

/* Minimal Linux waitid → wait4 bridge (siginfo filled for WEXITED). */
#define BFREE_P_ALL  0
#define BFREE_P_PID  1
#define BFREE_P_PGID 2
#define BFREE_WNOHANG_ID 0x00000001
#define BFREE_WEXITED    0x00000004
#define BFREE_SIGCHLD    17
#define BFREE_CLD_EXITED 1

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

    (void)prot;
    if (fd == BFREE_FB0_FD) {
        /* framebuffer path below */
    } else if (fd == (long)BFREE_GUEST_MEMFD_FD) {
        return sys_mmap_anonymous_heap(addr, length, flags | MAP_ANONYMOUS);
    } else if (fd < 0 || (flags & MAP_ANONYMOUS)) {
        return sys_mmap_anonymous_heap(addr, length, flags);
    } else {
        return -38; // -ENOSYS
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

// case 27: sys_shm_open
// POSIX shm_open はカーネル内では未実装。
long sys_shm_open(long name_ptr, long oflag, long mode)
{
    (void)name_ptr;
    (void)oflag;
    (void)mode;
    return -38; // -ENOSYS
}

// case 28: sys_shm_unlink
long sys_shm_unlink(long name_ptr)
{
    (void)name_ptr;
    return -38; // -ENOSYS
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
    if (g_guest_fork_active) {
        return (long)g_guest_fork_pid;
    }
    return 1;
}

static unsigned g_guest_futex_log_count;

// Linux 202: futex — single-threaded guest; never block, satisfy musl/Qt mutex init.
long sys_futex(long uaddr, long op, long val, long timeout_ptr, long uaddr2, long val3)
{
    int cmd = (int)(op & 0x7f);

    (void)val;
    (void)timeout_ptr;
    (void)uaddr2;
    (void)val3;

    switch (cmd) {
    case 0: /* FUTEX_WAIT */
    case 9: /* FUTEX_WAIT_BITSET */
        /*
         * Qt futexSemaphoreTryAcquire_loop: futex_wait returns but *uaddr stays locked
         * → fastTryLock fails forever (100% CPU, no further serial output).
         * Single-threaded guest: wake by clearing the word; never return EAGAIN.
         */
        if (uaddr != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)uaddr)) {
            *(int *)(uintptr_t)uaddr = 0;
        }
        if (g_guest_futex_log_count < 8U) {
            ++g_guest_futex_log_count;
            uart_puts("[FUTEX] wait\n");
        }
        return 0;
    case 1: /* FUTEX_WAKE */
        return 1;
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

#define BFREE_GUEST_DEV_NULL_FD     0x3700
#define BFREE_GUEST_DEV_URANDOM_FD  0x3701
#define BFREE_GUEST_PROC_MAPS_FD    0x3702
#define BFREE_GUEST_PASSWD_FD       0x3703
#define BFREE_GUEST_GROUP_FD        0x3704
#define BFREE_GUEST_PROFILE_FD      0x3705
#define BFREE_GUEST_BUSYBOX_FD      0x3706
#define BFREE_GUEST_VFILE_SLOTS     16
#define BFREE_GUEST_VFILE_SIZE      16384
#define BFREE_GUEST_VFILE_FD_BASE   0x3710
#define BFREE_GUEST_ROOT_DIR_FD     0x3720
#define BFREE_GUEST_TMP_DIR_FD      0x3721
#define BFREE_GUEST_BIN_DIR_FD      0x3722
#define BFREE_GUEST_MOTD_FD         0x3723
#define BFREE_GUEST_USR_DIR_FD      0x3724
#define BFREE_GUEST_VAR_DIR_FD      0x3725
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
#define BFREE_LINUX_O_DIRECTORY     040000

typedef struct {
    int used;
    int is_symlink; /* data[] holds the link target instead of file contents */
    int is_dir;     /* directory vnode under /tmp (name may contain '/') */
    int orphaned;   /* unlinked from namespace but still open */
    int open_refs;  /* live OFDs pointing at this vnode */
    char name[48];  /* path relative to /tmp, e.g. "a" or "a/b.txt" */
    unsigned char data[BFREE_GUEST_VFILE_SIZE];
    size_t len;
    /* Legacy fallback for internal magic fds. Published Linux fds use an
     * open-file description, so separate open() calls do not share this. */
    size_t pos;
} bfree_guest_vfile_t;

static bfree_guest_vfile_t g_guest_vfiles[BFREE_GUEST_VFILE_SLOTS];

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

/* Soft stubs until H04/H13 signal pending is restored */
static void bfree_guest_alarm_poll(void) {}
static int bfree_guest_sig_take_eintr(void) { return 0; }

#define BFREE_SYSRET_COOP_SWITCH ((long)-4092)
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_UNIX_SLOTS 8
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
static int g_coop_side;
static int g_coop_child_blocked;
static int g_coop_parent_started;
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static uint64_t g_coop_child_rcx, g_coop_child_r11, g_coop_child_rsp;
static uint64_t g_coop_child_rbx, g_coop_child_rbp, g_coop_child_r12;
static uint64_t g_coop_child_r13, g_coop_child_r14, g_coop_child_r15, g_coop_child_rdx;

static void bfree_coop_fd_snap_init(void)
{
    int i;
    bfree_guest_fd_ensure_init();
    for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
        g_fd_snap_parent[i] = g_guest_fd_target[i];
        g_fd_snap_child[i] = g_guest_fd_target[i];
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
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_parent[i];
        }
    } else {
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_fd_snap_parent[i] = g_guest_fd_target[i];
        }
        for (i = 0; i < BFREE_GUEST_FD_TABLE_SIZE; ++i) {
            g_guest_fd_target[i] = g_fd_snap_child[i];
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

static long bfree_coop_yield_to_parent(void)
{
    bfree_coop_save_child_user();
    if (g_coop_child_rcx >= 2) {
        g_coop_child_rcx -= 2;
    }
    g_coop_child_blocked = 1;
    bfree_coop_fd_switch_to(0);
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx;
    g_bfree_sysret_exec_r11 = g_bfree_fork_saved_r11;
    if (!g_coop_parent_started) {
        g_coop_parent_started = 1;
        g_bfree_fork_parent_ret = (uint64_t)(long)g_guest_fork_pid;
    } else {
        if (g_bfree_fork_saved_rcx >= 2) {
            g_bfree_sysret_exec_rcx = g_bfree_fork_saved_rcx - 2;
        }
        g_bfree_fork_parent_ret = 0;
    }
    return BFREE_SYSRET_COOP_SWITCH;
}

static long bfree_coop_yield_to_child(void)
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
    bfree_coop_fd_switch_to(1);
    g_coop_child_blocked = 0;
    g_bfree_sysret_exec_rsp = g_coop_child_rsp;
    g_bfree_sysret_exec_rcx = g_coop_child_rcx;
    g_bfree_sysret_exec_r11 = g_coop_child_r11;
    g_bfree_fork_saved_rbx = g_coop_child_rbx;
    g_bfree_fork_saved_rbp = g_coop_child_rbp;
    g_bfree_fork_saved_r12 = g_coop_child_r12;
    g_bfree_fork_saved_r13 = g_coop_child_r13;
    g_bfree_fork_saved_r14 = g_coop_child_r14;
    g_bfree_fork_saved_r15 = g_coop_child_r15;
    g_bfree_fork_saved_rdx = g_coop_child_rdx;
    g_bfree_fork_parent_ret = 0;
    return BFREE_SYSRET_COOP_SWITCH;
}

static int bfree_unix_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_UNIX_FD_BASE || fd >= (int)BFREE_UNIX_FD_BASE + BFREE_UNIX_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_UNIX_FD_BASE;
    return g_unix_socks[idx].used ? idx : -1;
}

static long sys_linux_socket(long domain, long type, long protocol)
{
    int i;
    (void)type;
    (void)protocol;
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
            return bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + i);
        }
    }
    return -24;
}

static long sys_linux_bind(long sockfd, long addr, long addrlen)
{
    int idx, i;
    const uint8_t *raw;
    size_t n;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
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
    sockfd = bfree_guest_fd_resolve((int)sockfd);
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

static long sys_linux_sendto(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    (void)flags;
    (void)addr;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_unix_socks[idx].pipe_magic, buf, len);
    }
    return sys_linux_write(fd, buf, len);
}

static long sys_linux_recvfrom(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    (void)flags;
    (void)addr;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].pipe_magic >= 0) {
        /* accepted side uses published pipe rd; connected client uses pipe_magic wr —
         * recv on accepted fd goes through normal pipe read via published fd. */
    }
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        /* Client sendto uses wr; client recv should use rd of same slot. */
        int mag = g_unix_socks[idx].pipe_magic;
        if (bfree_guest_pipe_is_wr_magic(mag)) {
            mag = mag - 1;
        }
        return sys_linux_read(mag, buf, len);
    }
    return sys_linux_read(fd, buf, len);
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

static void bfree_guest_vfile_clear_slot(bfree_guest_vfile_t *vf)
{
    if (!vf) {
        return;
    }
    vf->used = 0;
    vf->is_symlink = 0;
    vf->is_dir = 0;
    vf->orphaned = 0;
    vf->open_refs = 0;
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

static long sys_linux_memfd_create(long name_ptr, long flags)
{
    (void)name_ptr;
    (void)flags;
    g_guest_memfd_size = 0;
    return (long)BFREE_GUEST_MEMFD_FD;
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
        return 0;
    }
    return 0;
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
    "tmpfs /tmp tmpfs rw,relatime 0 0\n";
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
    bfree_guest_proc_maps_select_busybox(is_busybox);
    bfree_guest_pipe_reset_all();
    bfree_guest_fd_ensure_init();
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
    if (g_guest_fork_active) {
        return (long)g_guest_fork_pid;
    }
    return 1;
}

static long sys_linux_kill(long pid, long sig)
{
    (void)sig;
    if (pid == 1 || pid == 2 || pid == -1 ||
        (g_guest_fork_active && pid == (long)g_guest_fork_pid) ||
        (g_guest_fork_status_ready && pid == (long)g_guest_fork_pid)) {
        /* Signals are ignored in this single-task guest; succeed for known pids. */
        return 0;
    }
    if (pid == 0) {
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
    (void)pid;
    (void)resource;
    (void)new_limit;
    (void)old_limit;
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
    if (bfree_guest_is_pipe_rd(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)fd);

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf) || !ps) {
            return -14;
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
                bfree_guest_alarm_poll();
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

    fd = bfree_guest_fd_resolve((int)fd);
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
        return (long)n;
    }
    if (fd == 1 || fd == 2) {
        if (count > 0 && buf != 0 && bfree_user_vaddr_mapped((uint64_t)(uintptr_t)buf)) {
            bfree_guest_console_write((const uint8_t *)(uintptr_t)buf, (size_t)count);
            return (long)count;
        }
        return 0;
    }
    if (bfree_guest_is_pipe_wr(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd((int)fd);

        if (buf == 0 || count <= 0 || !bfree_user_ptr_mapped(buf) || !ps) {
            return -14;
        }
        if (ps->wr_open <= 0) {
            /* Broken pipe left on the shell's stdout: heal to console.
             * Do NOT heal while wr_open>0 — ash inproc pipelines redirect
             * fd1 to a live pipe with fork_active==0. */
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
        /* B-Free: coop yield after pipe write */
        if (g_guest_fork_active && g_coop_side == 0 && g_coop_child_blocked && n > 0) {
            return bfree_coop_yield_to_child();
        }
        return (long)n;
    }
    if (bfree_guest_is_eventfd(fd)) {
        int idx = bfree_guest_eventfd_index(fd);
        const uint64_t *src;

        if (buf == 0 || count < (long)sizeof(uint64_t) || !bfree_user_ptr_mapped(buf)) {
            return -14;
        }
        src = (const uint64_t *)(uintptr_t)buf;
        g_guest_eventfd_val[idx] += *src;
        return (long)sizeof(uint64_t);
    }
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
    if (path[0] == '/' && path[1] == 'r' && path[2] == 'o' && path[3] == 'o' &&
        path[4] == 't' && path[5] == '/') {
        return -2; /* ENOENT — Qt embeds build-machine paths (qtlogging.ini) */
    }
    accmode = (int)(flags & BFREE_LINUX_O_ACCMODE);
    want_dir = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_DIRECTORY) != 0UL;
    want_create = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_CREAT) != 0UL;
    want_trunc = ((unsigned long)flags & (unsigned long)BFREE_LINUX_O_TRUNC) != 0UL;

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
        strcmp(path, "/etc/profile") == 0 || strcmp(path, "/etc/motd") == 0) {
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

static long sys_linux_close(long fd)
{
    int orig = (int)fd;
    int resolved;

    resolved = bfree_guest_fd_resolve(orig);
    if (bfree_guest_is_pipe_wr(resolved) || bfree_guest_is_pipe_rd(resolved)) {
        bfree_guest_pipe_ref(resolved, -1);
    }
    if (orig >= 0 && orig < BFREE_GUEST_FD_TABLE_SIZE) {
        g_guest_fd_target[orig] = -1;
        g_guest_fd_dup_save[orig] = -1;
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

static long sys_linux_getuid(void)
{
    return 0;
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

static long sys_linux_rt_sigaction(long signum, long act, long oldact, long sigsetsize)
{
    (void)signum;
    (void)act;
    (void)oldact;
    (void)sigsetsize;
    return 0;
}

static long sys_linux_rt_sigprocmask(long how, long set, long oldset, long sigsetsize)
{
    (void)how;
    (void)set;
    (void)oldset;
    (void)sigsetsize;
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

typedef char bfree_linux_stat_size_ok[(sizeof(bfree_linux_stat_t) == 144U) ? 1 : -1];

static long bfree_linux_stat_fill(long statbuf, uint32_t mode, int64_t size)
{
    bfree_linux_stat_t *st;

    if (statbuf == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)statbuf, sizeof(bfree_linux_stat_t))) {
        return -14;
    }
    st = (bfree_linux_stat_t *)(uintptr_t)statbuf;
    memset(st, 0, sizeof(*st));
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
        ((bfree_linux_stat_t *)(uintptr_t)statbuf)->st_ino =
            100ULL + (uint64_t)(vf - g_guest_vfiles);
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
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 262544);
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
    if (strcmp(path, "/usr") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (strcmp(path, "/usr/bin") == 0) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    if (path[0] == '/' && path[1] == 'u' && path[2] == 's' && path[3] == 'r' &&
        path[4] == '/' && path[5] == 'b' && path[6] == 'i' && path[7] == 'n' &&
        path[8] == '/' && path[9] != '\0') {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 262544);
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
    {
        char vname[64];

        if (bfree_guest_path_is_under_tmp(path, vname, sizeof(vname)) && vname[0] != '\0') {
            bfree_guest_vfile_t *vf = bfree_guest_vfile_find_by_name(vname);

            if (vf) {
                return bfree_linux_stat_fill_vfile(statbuf, vf);
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
        target == (int)BFREE_GUEST_VAR_DIR_FD || target == (int)BFREE_GUEST_PROC_DIR_FD ||
        target == (int)BFREE_GUEST_PROC_PID_DIR_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFDIR | 0755U, 4096);
    }
    fd = (long)target;
    if (fd == (long)BFREE_GUEST_BUSYBOX_FD) {
        return bfree_linux_stat_fill(statbuf, BFREE_LINUX_S_IFREG | 0755U, 262544);
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
        ".", "..", "bin", "dev", "etc", "proc", "root", "tmp", "usr", "var", "busybox.elf"
    };
    static const char *const k_bin_names[] = {
        ".", "..", "sh", "busybox", "echo", "cat", "ls", "grep", "mkdir", "rm", "cp", "mv"
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
                               strcmp(name, "etc") == 0 || strcmp(name, "proc") == 0 ||
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
    } else if (target == (int)BFREE_GUEST_TMP_DIR_FD || (dvf && dvf->is_dir)) {
        tmp_parent = (dvf && dvf->is_dir) ? dvf->name : "";
        name_count = 2U;
        for (i = 0; i < BFREE_GUEST_VFILE_SLOTS; ++i) {
            if (g_guest_vfiles[i].used &&
                bfree_guest_tmp_child_basename(g_guest_vfiles[i].name, tmp_parent,
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
                if (!g_guest_vfiles[i].used) {
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
            target == (int)BFREE_GUEST_VAR_DIR_FD || target == (int)BFREE_GUEST_PROC_DIR_FD ||
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
    /* Drop from the namespace immediately; keep storage while OFDs remain. */
    vf->name[0] = '\0';
    if (vf->open_refs > 0) {
        vf->orphaned = 1;
        return 0;
    }
    bfree_guest_vfile_clear_slot(vf);
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
    // fd 0, 1, 2 は端末（シリアルコンソール）とみなす
    if (fd >= 0 && fd <= 2) {
        return 1; // 端末
    }
    return 0; // 端末ではない
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
    int index = fd - BFREE_TIMERFD_FD_BASE;
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

        while (!bfree_stdin_byte_ready()) {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
        if (!bfree_stdin_pop_byte(&ch)) {
            break;
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
            return g_timerfd_entries[index].fd;
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
/* Qt desktop.elf: __init_tls + static ctors; 6144 pages overflows (top too low) — use full span below FB. */
#define BFREE_USER_STACK_PAGES_EXEC 4608
#define BFREE_USER_STACK_PAGES_BUSYBOX 512

/* Real user stack page: User|RW|Present and not identity VA==PA (clone artifact). */
static int bfree_user_stack_page_user_mapped(page_table_t *pt, uint64_t vaddr)
{
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte;
    uint64_t paddr;

    if (!pt || vaddr >= VMM_USER_VA_BYTES) {
        return 0;
    }
    pd_index = (vaddr >> 21) & 0x1FFULL;
    pt_index = (vaddr >> 12) & 0x1FFULL;
    if (pd_index >= PT_LEVEL_COUNT) {
        return 0;
    }
    pte = pt->pt[pd_index][pt_index];
    if ((pte & 0x007ULL) != 0x007ULL) {
        return 0;
    }
    paddr = pte & ~(PAGE_SIZE - 1ULL);
    if (paddr == (vaddr & ~(PAGE_SIZE - 1ULL))) {
        return 0;
    }
    return 1;
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
    uint64_t pd_index;
    uint64_t pt_index;
    uint64_t pte;

    if (!knl_current_task || !knl_current_task->page_table_base || !out_phys) {
        return -1;
    }
    if (vaddr >= VMM_USER_VA_BYTES) {
        return -1;
    }
    pt = (page_table_t *)knl_current_task->page_table_base;
    pd_index = (vaddr >> 21) & 0x1FFULL;
    pt_index = (vaddr >> 12) & 0x1FFULL;
    if (pd_index >= PT_LEVEL_COUNT) {
        return -1;
    }
    pte = pt->pt[pd_index][pt_index];
    if ((pte & 0x007ULL) != 0x007ULL) {
        return -1;
    }
    *out_phys = pte & ~(PAGE_SIZE - 1ULL);
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
    unsigned i = 0;
    if (!a || !b) {
        return 0;
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

    is_child = g_guest_fork_active && bfree_process_child_active();

    if (copy_user_cstr(path_ptr, path, sizeof(path)) != 0) {
        return -14;
    }
    if (bfree_copy_user_strarray(argv_ptr, argv_buf, 16, &argc) != 0 || argc <= 0) {
        return -14;
    }
    /* musl busybox expects argv[0]=/busybox.elf when re-entering from execve. */
    if (argc + 1 <= 16) {
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
        ld = load_elf_image("busybox.elf", &entry, load_pt);
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
        ld = load_elf_image("busybox.elf", &entry, load_pt);
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
        if (bfree_user_exec_prepare_musl_stack_argv(exec_stack_top, argc, argv_ptrs, envc,
                                                    env_ptrs, &elf, &user_rsp) != 0) {
            if (use_private_as) {
                knl_current_task->page_table_base = parent_pt;
                bfree_process_exit_restore_as();
            }
            return -1;
        }
    }

    (void)path;
    bfree_enable_user_fpu();
    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);
    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)entry;
    g_bfree_sysret_exec_r11 = 0x202ULL;
    /* Child: switch CR3 with RSP in assembly. Standalone: already on task CR3. */
    g_bfree_sysret_exec_cr3 = use_private_as ? (uint64_t)(uintptr_t)child_pt : 0;
    return BFREE_SYSRET_EXEC_TRANSFER;
}

#define BFREE_LINUX_CLONE_VM     0x00000100
#define BFREE_LINUX_CLONE_VFORK  0x00004000

static long sys_linux_clone(long flags, long newsp, long ptid, long ctid, long tls)
{
    (void)newsp;
    (void)ptid;
    (void)ctid;
    (void)tls;
    /* Only the cooperative vfork-like subset is supported. Real fork/threads
     * need address-space copy and a scheduler. */
    if (((unsigned long)flags & (unsigned long)BFREE_LINUX_CLONE_VFORK) != 0UL) {
        return bfree_guest_fork_enter();
    }
    return -38; /* ENOSYS */
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
            : BFREE_USER_STACK_PAGES_EXEC) != 0) {
        uart_puts("[SYSCALL] exec_initrd: stack ensure failed\n");
        return -1;
    }
    if (bfree_user_exec_prepare_stack(stack_top, entry, &user_rsp) != 0) {
        uart_puts("[SYSCALL] exec_initrd: prepare stack failed\n");
        return -1;
    }

    bfree_security_set_role(BFREE_ROLE_APP);

    bfree_enable_user_fpu();

    if (knl_current_task != 0) {
        knl_current_task->user_fsbase = 0;
    }
    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, 0);

    g_bfree_sysret_exec_rsp = user_rsp;
    g_bfree_sysret_exec_rcx = (uint64_t)(uintptr_t)entry;
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

    for (i = 0; i < BFREE_MAX_GUEST_EPOLL; ++i) {
        if (g_guest_epoll[i].used && g_guest_epoll[i].fd == epfd) {
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

/* Pipe slots are never explicitly destroyed (refcounts just decay to 0 when a
 * pipeline finishes), so reclaim dead slots here or pipelines exhaust them. */
static void bfree_guest_pipe_reclaim_dead_slots(void)
{
    int i;
    int t;

    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        int rd_refs = 0;
        int wr_refs = 0;
        int rd_magic;
        int wr_magic;

        if (!g_guest_pipes[i].used) {
            continue;
        }
        rd_magic = bfree_guest_pipe_magic_fd(i, 0);
        wr_magic = bfree_guest_pipe_magic_fd(i, 1);
        /* Authoritative open counts from the shared fd table (vfork-safe). */
        for (t = 0; t < BFREE_GUEST_FD_TABLE_SIZE; ++t) {
            int tgt = g_guest_fd_target[t];
            int saved = g_guest_fd_dup_save[t];

            if (tgt == rd_magic || saved == rd_magic) {
                rd_refs++;
            }
            if (tgt == wr_magic || saved == wr_magic) {
                wr_refs++;
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
        }
        g_guest_pipes[i].used = 0;
        g_guest_pipes[i].nonblock = 0;
        g_guest_pipes[i].len = 0;
    }
}

static long sys_linux_pipe2(long pipefd_ptr, long flags)
{
    int *pipefd = (int *)(uintptr_t)pipefd_ptr;
    int i;
    int rd;
    int wr;

    if (pipefd == 0 || !bfree_user_ptr_mapped(pipefd_ptr)) {
        return -14;
    }
    bfree_guest_pipe_reclaim_dead_slots();
    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (!g_guest_pipes[i].used) {
            g_guest_pipes[i].used = 1;
            g_guest_pipes[i].len = 0;
            g_guest_pipes[i].nonblock = ((unsigned long)flags & 020000UL) != 0UL;
            g_guest_pipes[i].wr_open = 1;
            g_guest_pipes[i].rd_open = 1;
            rd = bfree_guest_pipe_magic_fd(i, 0);
            wr = bfree_guest_pipe_magic_fd(i, 1);
            pipefd[0] = bfree_guest_fd_publish(rd);
            pipefd[1] = bfree_guest_fd_publish(wr);
            if (pipefd[0] < 0 || pipefd[1] < 0) {
                g_guest_pipes[i].used = 0;
                return -24;
            }
            return 0;
        }
    }
    return -24;
}

static long sys_linux_socketpair(long domain, long type, long protocol, long sv_ptr)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return sys_linux_pipe2(sv_ptr, 0);
}

static long sys_linux_eventfd2(long count, long flags)
{
    static int next_eventfd;

    (void)count;
    (void)flags;
    if (next_eventfd >= BFREE_MAX_GUEST_EVENTFD) {
        return -24;
    }
    g_guest_eventfd_val[next_eventfd] = (count != 0) ? (uint64_t)count : 0ULL;
    return (int)BFREE_GUEST_EVENTFD_BASE + next_eventfd++;
}

static long sys_linux_epoll_create1(long flags)
{
    int i;

    (void)flags;
    for (i = 0; i < BFREE_MAX_GUEST_EPOLL; ++i) {
        if (!g_guest_epoll[i].used) {
            g_guest_epoll[i].used = 1;
            g_guest_epoll[i].fd = (int)BFREE_GUEST_EPOLL_FD_BASE + i;
            return g_guest_epoll[i].fd;
        }
    }
    return -24; /* EMFILE */
}

static long sys_linux_epoll_ctl(long epfd, long op, long fd, long event_ptr)
{
    bfree_guest_epoll_inst_t *inst = bfree_guest_find_epoll(epfd);
    struct epoll_event *ev = (struct epoll_event *)(uintptr_t)event_ptr;
    bfree_guest_epoll_watch_t *watch;
    int i;

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
    if (ev == 0 || !bfree_user_ptr_mapped(event_ptr)) {
        return -14;
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
    watch->events = ev->events;
    watch->data = ev->data;
    return 0;
}

static long sys_linux_epoll_wait(long epfd, long events_ptr, long maxevents, long timeout_ms)
{
    bfree_guest_epoll_inst_t *inst = bfree_guest_find_epoll(epfd);
    struct epoll_event *out = (struct epoll_event *)(uintptr_t)events_ptr;
    int ready = 0;
    int i;

    if (!inst) {
        return -9;
    }
    if (out == 0 || maxevents <= 0 || !bfree_user_ptr_mapped(events_ptr)) {
        return -14;
    }
    for (i = 0; i < BFREE_MAX_GUEST_EPOLL_WATCHES && ready < (int)maxevents; ++i) {
        bfree_guest_epoll_watch_t *w = &inst->watches[i];
        uint32_t revents = 0;

        if (!w->used) {
            continue;
        }
        if (bfree_guest_is_pipe_rd(w->fd) && bfree_guest_pipe_readable(w->fd)) {
            revents = EPOLLIN;
        } else if (bfree_guest_is_eventfd(w->fd)
                   && g_guest_eventfd_val[bfree_guest_eventfd_index(w->fd)] != 0) {
            revents = EPOLLIN;
        }
        if ((revents & w->events) != 0) {
            out[ready].events = revents;
            out[ready].data = w->data;
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

                if (!w->used) {
                    continue;
                }
                if (bfree_guest_is_pipe_rd(w->fd) && bfree_guest_pipe_readable(w->fd)) {
                    revents = EPOLLIN;
                } else if (bfree_guest_is_eventfd(w->fd)
                           && g_guest_eventfd_val[bfree_guest_eventfd_index(w->fd)] != 0) {
                    revents = EPOLLIN;
                }
                if ((revents & w->events) != 0) {
                    out[ready].events = revents;
                    out[ready].data = w->data;
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

    if ((events & POLLIN) && bfree_guest_is_pipe_rd(fd) && bfree_guest_pipe_readable(fd)) {
        revents |= POLLIN;
    }
    if ((events & POLLOUT) && bfree_guest_is_pipe_wr(fd)) {
        bfree_guest_pipe_slot_t *ps = bfree_guest_pipe_slot_from_fd(fd);

        if (ps && ps->len < BFREE_GUEST_PIPE_BUF_SIZE) {
            revents |= POLLOUT;
        }
    }
    if ((events & POLLIN) && bfree_guest_is_eventfd(fd)) {
        int idx = bfree_guest_eventfd_index(fd);

        if (idx >= 0 && g_guest_eventfd_val[idx] != 0) {
            revents |= POLLIN;
        }
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
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

static long sys_linux_prctl(long option, long arg2, long arg3, long arg4, long arg5)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    /* musl/Qt probe thread naming, dumpable, etc. — no-op on single-thread guest. */
    if (option == 15 || option == 16) { /* PR_SET_NAME / PR_GET_NAME */
        return 0;
    }
    return 0;
}

/* Linux guest nanosleep — instant return (no IRQ busy-wait in syscall context). */
static long sys_linux_nanosleep(long req_ptr, long rem_ptr)
{
    struct timespec *rem = (struct timespec *)rem_ptr;

    (void)req_ptr;
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

static long bfree_dispatch_linux_guest_syscall(long num, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    bfree_guest_trace_sc_num(num);
    /* Sparse cases past the 0..332 jump table — handle before switch. */
    if (num == 319) {
        return sys_linux_memfd_create(arg1, arg2);
    }
    if (num == 77) {
        return sys_linux_ftruncate(arg1, arg2);
    }
    switch (num) {
    case 0:
        return sys_linux_read(arg1, arg2, arg3);
    case 1:
        return sys_linux_write(arg1, arg2, arg3);
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
    case 109: /* setpgid — job-control stub (single session) */
        return 0;
    case 111: /* getpgid */
        return (arg1 == 0) ? sys_linux_getpid() : arg1;
    case 112: /* setsid */
        return sys_linux_getpid();
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
    case 41: /* socket */
        return sys_linux_socket(arg1, arg2, arg3);
    case 49: /* bind */
        return sys_linux_bind(arg1, arg2, arg3);
    case 50: /* listen */
        return sys_linux_listen(arg1, arg2);
    case 43: /* accept */
        return sys_linux_accept(arg1, arg2, arg3);
    case 42: /* connect */
        return sys_linux_connect(arg1, arg2, arg3);
    case 44: /* sendto */
        return sys_linux_sendto(arg1, arg2, arg3, arg4, arg5, 0);
    case 45: /* recvfrom */
        return sys_linux_recvfrom(arg1, arg2, arg3, arg4, arg5, 0);
    case 53:
        return sys_linux_socketpair(arg1, arg2, arg3, arg4);
    case 284:
        return sys_linux_eventfd2(arg1, arg2);
    case 290: /* musl __NR_eventfd2 (284 is __NR_eventfd v1) */
        return sys_linux_eventfd2(arg1, arg2);
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
    case 16:
        return sys_linux_ioctl(arg1, arg2, arg3);
    case 20:
        return sys_linux_writev(arg1, arg2, arg3);
    case 21:
        return sys_linux_access(arg1, arg2);
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
    case 92: /* chown — single-user guest, ownership is fixed at root */
    case 93: /* fchown */
    case 94: /* lchown */
    case 260: /* fchownat */
        return 0;
    case 96:
        return sys_gettimeofday(arg1, arg2);
    case 97:
        return sys_getrlimit(arg1, arg2);
    case 99: /* sysinfo */
        return sys_linux_sysinfo(arg1);
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
    case 257:
        return sys_linux_openat(arg1, arg2, arg3, arg4);
    case 318:
        return sys_linux_getrandom(arg1, arg2, arg3);
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
    case 57: /* fork — no address-space copy yet */
        return -38; /* ENOSYS */
    case 58: /* vfork */
        return bfree_guest_fork_enter();
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

    /* B-Free native numbers overlap Linux 0/1/24; disambiguate before Linux dispatch. */
    if (num == 24) {
        if (arg2 > 0 && arg2 <= 4096 && bfree_user_ptr_mapped(arg1)) {
            return sys_debug_serial_write(arg1, arg2);
        }
        return 0; /* Linux sched_yield */
    }
    if (num == 26) {
        return sys_mmap(arg1, arg2, arg3, arg4, arg5);
    }
    if (num == 1001) {
        return sys_get_framebuffer_info(arg1);
    }
    if (num == 0 && arg1 > 2 && bfree_user_ptr_mapped(arg1)) {
        return sys_poll_input_event(arg1);
    }
    if (num == 1 && arg1 > 2 && bfree_user_ptr_mapped(arg1)) {
        return sys_get_framebuffer_info(arg1);
    }

    {
        long linux_ret = bfree_dispatch_linux_guest_syscall(num, arg1, arg2, arg3, arg4, arg5);
        if (linux_ret != BFREE_LINUX_SYSCALL_UNHANDLED) {
            return linux_ret;
        }
    }
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
