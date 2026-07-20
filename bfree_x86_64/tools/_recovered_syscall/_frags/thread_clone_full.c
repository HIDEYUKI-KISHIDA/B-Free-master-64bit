#include "bfree_guest_thread.h"
#include "syscall.h"
#include "vmm.h"
#include <stddef.h>
#include <stdint.h>

extern uint64_t g_bfree_user_sysret_rcx;
extern uint64_t g_bfree_user_sysret_r11;
extern uint64_t g_bfree_user_sysret_rsp;
extern uint64_t g_bfree_user_sysret_rbx;
extern uint64_t g_bfree_user_sysret_rbp;
extern uint64_t g_bfree_user_sysret_r12;
extern uint64_t g_bfree_user_sysret_r13;
extern uint64_t g_bfree_user_sysret_r14;
extern uint64_t g_bfree_user_sysret_r15;
extern uint64_t g_bfree_user_sysret_rdx;

extern uint64_t g_bfree_fork_saved_rcx;
extern uint64_t g_bfree_fork_saved_r11;
extern uint64_t g_bfree_fork_saved_rsp;
extern uint64_t g_bfree_fork_saved_rbx;
extern uint64_t g_bfree_fork_saved_rbp;
extern uint64_t g_bfree_fork_saved_r12;
extern uint64_t g_bfree_fork_saved_r13;
extern uint64_t g_bfree_fork_saved_r14;
extern uint64_t g_bfree_fork_saved_r15;
extern uint64_t g_bfree_fork_saved_rdx;
extern uint64_t g_bfree_fork_parent_ret;
extern uint64_t g_bfree_sysret_exec_rsp;
extern uint64_t g_guest_fork_saved_fsbase;
extern uintptr_t g_guest_clear_child_tid;
extern int g_guest_fork_active;
extern int g_guest_next_pid;

extern int bfree_user_ptr_mapped(long addr);
extern void bfree_wrmsr64(uint32_t msr, uint64_t val);
extern void bfree_futex_wake_addr(volatile int *ua);

#ifndef BFREE_MSR_FS_BASE
#define BFREE_MSR_FS_BASE 0xC0000100ULL
#endif

#define BFREE_LINUX_CLONE_VM            0x00000100
#define BFREE_LINUX_CLONE_THREAD        0x00010000
#define BFREE_LINUX_CLONE_SETTLS        0x00080000
#define BFREE_LINUX_CLONE_PARENT_SETTID 0x00100000
#define BFREE_LINUX_CLONE_CHILD_CLEARTID 0x00200000

#define BFREE_SYSRET_FORK_PARENT ((long)-4093)

static int g_guest_thread_active;
static int g_guest_thread_tid;
static int g_guest_thread_slots_used;

void bfree_guest_thread_init(void)
{
    g_guest_thread_active = 0;
    g_guest_thread_tid = 0;
    g_guest_thread_slots_used = 0;
}

int bfree_guest_thread_active(void)
{
    return g_guest_thread_active;
}

int bfree_guest_thread_tid(void)
{
    return g_guest_thread_tid;
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

static void bfree_guest_thread_publish_parent_resume(int tid)
{
    g_bfree_sysret_exec_rsp = g_bfree_fork_saved_rsp;
    g_bfree_fork_parent_ret = (uint64_t)(long)tid;
}

long bfree_guest_thread_clone(unsigned long flags, long newsp, long ptid, long ctid, long tls)
{
    int tid;
    uint64_t child_rsp;
    uint64_t parent_fs;

    (void)flags;
    if (g_guest_fork_active || g_guest_thread_active) {
        return -11; /* EAGAIN — no nested thread/fork mix yet */
    }
    if (g_guest_thread_slots_used >= BFREE_GUEST_MAX_THREADS) {
        return -11;
    }
    if (newsp == 0 || !bfree_user_ptr_mapped(newsp)) {
        return -14;
    }
    child_rsp = (uint64_t)(uintptr_t)newsp;
    child_rsp &= ~0xFULL;

    tid = g_guest_next_pid++;
    if (tid <= 0) {
        return -11;
    }

    parent_fs = g_guest_fork_saved_fsbase;
    bfree_guest_thread_save_parent_ctx();

    if ((flags & (unsigned long)BFREE_LINUX_CLONE_SETTLS) != 0UL && tls != 0) {
        if (!bfree_user_ptr_mapped(tls)) {
            return -14;
        }
        bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, (uint64_t)(uintptr_t)tls);
    }

    if ((flags & (unsigned long)BFREE_LINUX_CLONE_PARENT_SETTID) != 0UL &&
        ptid != 0 && bfree_user_ptr_mapped(ptid)) {
        *(int *)(uintptr_t)ptid = tid;
    }
    if ((flags & (unsigned long)BFREE_LINUX_CLONE_CHILD_CLEARTID) != 0UL &&
        ctid != 0 && bfree_user_ptr_mapped(ctid)) {
        g_guest_clear_child_tid = (uintptr_t)ctid;
        *(int *)(uintptr_t)ctid = tid;
    } else {
        g_guest_clear_child_tid = 0;
    }

    g_guest_thread_active = 1;
    g_guest_thread_tid = tid;
    g_guest_thread_slots_used++;

    g_bfree_sysret_exec_rsp = child_rsp;
    g_guest_fork_saved_fsbase = parent_fs;

    return BFREE_SYSRET_THREAD_CHILD;
}

long bfree_guest_thread_exit(long status)
{
    int tid;
    int *cleartid;
    uint64_t parent_fs;

    (void)status;
    if (!g_guest_thread_active) {
        return -1;
    }
    tid = g_guest_thread_tid;
    parent_fs = g_guest_fork_saved_fsbase;

    if (g_guest_clear_child_tid != 0 &&
        bfree_user_ptr_mapped((long)g_guest_clear_child_tid)) {
        cleartid = (int *)(uintptr_t)g_guest_clear_child_tid;
        *cleartid = 0;
        bfree_futex_wake_addr((volatile int *)cleartid);
        g_guest_clear_child_tid = 0;
    }

    g_guest_thread_active = 0;
    g_guest_thread_tid = 0;
    if (g_guest_thread_slots_used > 0) {
        g_guest_thread_slots_used--;
    }

    bfree_wrmsr64((uint32_t)BFREE_MSR_FS_BASE, parent_fs);
    bfree_guest_thread_publish_parent_resume(tid);
    return BFREE_SYSRET_FORK_PARENT;
}
