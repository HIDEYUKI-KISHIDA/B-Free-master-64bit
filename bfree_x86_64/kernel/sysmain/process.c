#include "process.h"
#include "vmm.h"

#include "../include/tk/kernel.h"

extern TCB *knl_current_task;
extern page_table_t kernel_page_table;
void uart_puts(const char *s);
void uart_puthex64(uint64_t v);

/*
 * Keep process control state in ordinary BSS — never in .data next to boot_pd.
 * An initialized `g_active = -1` landed at 0x14b000 (immediately after boot_pd)
 * where OOB / aliasing could sticky-leave it at 0 and block the next AS-copy
 * fork with EAGAIN after a vfork+exec applet (wc then echo|cat).
 */
static bfree_proc_child_t g_children[BFREE_PROC_MAX_CHILDREN];
static int g_active; /* focused VFORK/RUNNING child index, or -1 */
static int g_next_pid;

/* Dedicated address space for vfork+exec (not AS-copy pool). */
static int g_child_pt_busy;
static int g_child_pt_kernel_ready;

/* AS-copy fork PT pool — must NOT alias g_child_page_table (shell may live there). */
static int g_fork_pt_kernel_ready[BFREE_PROC_MAX_LIVE];
static int g_fork_pt_used[BFREE_PROC_MAX_LIVE];
static page_table_t g_child_page_table __attribute__((aligned(4096), section(".bfree_page_table")));
static page_table_t g_fork_page_table[BFREE_PROC_MAX_LIVE]
    __attribute__((aligned(4096), section(".bfree_page_table")));

static int bfree_process_is_live_state(bfree_proc_state_t st)
{
    /* Occupies a live slot (counts toward MAX_LIVE). */
    return st == BFREE_PROC_VFORK || st == BFREE_PROC_RUNNING ||
           st == BFREE_PROC_STOPPED;
}

static int bfree_process_is_runnable_state(bfree_proc_state_t st)
{
    return st == BFREE_PROC_VFORK || st == BFREE_PROC_RUNNING;
}

static void bfree_process_heal_active(void)
{
    if (g_active < 0 || g_active >= BFREE_PROC_MAX_CHILDREN) {
        g_active = -1;
        return;
    }
    if (!bfree_process_is_live_state(g_children[g_active].state)) {
        g_active = -1;
    }
}

static bfree_proc_child_t *bfree_process_active(void)
{
    bfree_process_heal_active();
    if (g_active < 0 || g_active >= BFREE_PROC_MAX_CHILDREN) {
        return 0;
    }
    return &g_children[g_active];
}

int bfree_process_live_count(void)
{
    int i;
    int n = 0;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (bfree_process_is_live_state(g_children[i].state)) {
            n++;
        }
    }
    return n;
}

int bfree_process_runnable_count(void)
{
    int i;
    int n = 0;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (bfree_process_is_runnable_state(g_children[i].state)) {
            n++;
        }
    }
    return n;
}

int bfree_process_vfork_count(void)
{
    int i;
    int n = 0;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state == BFREE_PROC_VFORK) {
            n++;
        }
    }
    return n;
}

int bfree_process_slot_of_pid(int pid)
{
    int i;

    if (pid <= 0) {
        return -1;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_FREE && g_children[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

int bfree_process_select_slot(int slot)
{
    if (slot < 0 || slot >= BFREE_PROC_MAX_CHILDREN) {
        return -1;
    }
    /* Allow focus on STOPPED too (for cont / wait). */
    if (!bfree_process_is_live_state(g_children[slot].state)) {
        return -1;
    }
    g_active = slot;
    return 0;
}

int bfree_process_select_pid(int pid)
{
    int slot = bfree_process_slot_of_pid(pid);

    if (slot < 0) {
        return -1;
    }
    return bfree_process_select_slot(slot);
}

int bfree_process_first_live_pid(void)
{
    return bfree_process_nth_live_pid(0);
}

int bfree_process_nth_live_pid(int n)
{
    int i;
    int seen = 0;

    if (n < 0) {
        return -1;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (!bfree_process_is_live_state(g_children[i].state)) {
            continue;
        }
        if (seen == n) {
            return g_children[i].pid;
        }
        seen++;
    }
    return -1;
}

int bfree_process_first_runnable_pid(void)
{
    int i;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (bfree_process_is_runnable_state(g_children[i].state)) {
            return g_children[i].pid;
        }
    }
    return -1;
}

static void bfree_process_release_fork_pt(int idx)
{
    if (idx < 0 || idx >= BFREE_PROC_MAX_LIVE) {
        return;
    }
    g_fork_pt_used[idx] = 0;
}

static void bfree_process_soft_reap_zombie_slot(int i)
{
    int pt_idx;

    if (i < 0 || i >= BFREE_PROC_MAX_CHILDREN) {
        return;
    }
    if (g_children[i].state != BFREE_PROC_ZOMBIE) {
        return;
    }
    /* Soft-reap must free AS-copy PT or sequential fork+wait+fork sticky-EAGAIN. */
    pt_idx = g_children[i].fork_pt_idx;
    if (pt_idx >= 0) {
        bfree_process_release_fork_pt(pt_idx);
    }
    g_children[i].state = BFREE_PROC_FREE;
    g_children[i].pid = 0;
    g_children[i].child_pt = 0;
    g_children[i].parent_pt = 0;
    g_children[i].has_private_as = 0;
    g_children[i].fork_pt_idx = -1;
    g_children[i].coop_session = -1;
}

static int bfree_process_find_free_slot(void)
{
    int i;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state == BFREE_PROC_FREE) {
            return i;
        }
    }
    /* Soft-reap oldest zombie so pipelines do not stall forever. */
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state == BFREE_PROC_ZOMBIE) {
            bfree_process_soft_reap_zombie_slot(i);
            return i;
        }
    }
    return -1;
}

static void bfree_process_reclaim_orphan_fork_pts(void)
{
    int i;
    int pt;
    int held;

    for (pt = 0; pt < BFREE_PROC_MAX_LIVE; ++pt) {
        if (!g_fork_pt_used[pt]) {
            continue;
        }
        held = 0;
        for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
            if (g_children[i].state == BFREE_PROC_FREE) {
                continue;
            }
            if (g_children[i].fork_pt_idx == pt) {
                held = 1;
                break;
            }
        }
        if (!held) {
            g_fork_pt_used[pt] = 0;
        }
    }
}

static int bfree_process_alloc_fork_pt(void)
{
    int i;

    bfree_process_reclaim_orphan_fork_pts();
    for (i = 0; i < BFREE_PROC_MAX_LIVE; ++i) {
        if (!g_fork_pt_used[i]) {
            g_fork_pt_used[i] = 1;
            return i;
        }
    }
    return -1;
}

void bfree_process_init(void)
{
    int i;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        g_children[i].state = BFREE_PROC_FREE;
        g_children[i].pid = 0;
        g_children[i].ppid = 1;
        g_children[i].pgid = 1;
        g_children[i].exit_status = 0;
        g_children[i].exited_signal = 0;
        g_children[i].stop_sig = 0;
        g_children[i].stop_pending = 0;
        g_children[i].has_private_as = 0;
        g_children[i].fork_pt_idx = -1;
        g_children[i].coop_session = -1;
        g_children[i].parent_pt = 0;
        g_children[i].child_pt = 0;
    }
    g_active = -1;
    g_next_pid = 2;
    g_child_pt_busy = 0;
    g_child_pt_kernel_ready = 0;
    for (i = 0; i < BFREE_PROC_MAX_LIVE; ++i) {
        g_fork_pt_kernel_ready[i] = 0;
        g_fork_pt_used[i] = 0;
    }
}

long bfree_process_vfork_enter(int *child_pid)
{
    int slot;
    bfree_proc_child_t *ch;

    /* One shared-AS VFORK at a time; total live ≤ MAX_LIVE (H02). */
    if (bfree_process_vfork_count() > 0) {
        return -11; /* EAGAIN */
    }
    if (bfree_process_live_count() >= BFREE_PROC_MAX_LIVE) {
        return -11; /* EAGAIN */
    }
    slot = bfree_process_find_free_slot();
    if (slot < 0) {
        return -11; /* EAGAIN */
    }

    ch = &g_children[slot];
    ch->pid = g_next_pid++;
    if (ch->pid <= 1) {
        g_next_pid = 2;
        ch->pid = g_next_pid++;
    }
    ch->ppid = 1;
    ch->pgid = 1;
    ch->exit_status = 0;
    ch->exited_signal = 0;
    ch->stop_sig = 0;
    ch->stop_pending = 0;
    ch->has_private_as = 0;
    ch->fork_pt_idx = -1;
    ch->coop_session = -1;
    {
        page_table_t *live_cr3 = 0;
        __asm__ volatile("mov %%cr3, %0" : "=r"(live_cr3));
        if (live_cr3 && knl_current_task) {
            knl_current_task->page_table_base = live_cr3;
        }
        ch->parent_pt = live_cr3
            ? live_cr3
            : (knl_current_task ? (page_table_t *)knl_current_task->page_table_base : 0);
    }
    ch->child_pt = 0;
    ch->state = BFREE_PROC_VFORK;
    g_active = slot;
    if (child_pid) {
        *child_pid = ch->pid;
    }
    return 0;
}

long bfree_process_fork_enter(int *child_pid)
{
    int slot;
    int pt_idx;
    bfree_proc_child_t *ch;
    page_table_t *parent;
    page_table_t *child;
    page_table_t *live_cr3;

    bfree_process_heal_active();
    bfree_process_reclaim_orphan_fork_pts();
    if (bfree_process_live_count() >= BFREE_PROC_MAX_LIVE) {
        uart_puts("[FORK] EAGAIN live=");
        uart_puthex64((uint64_t)(unsigned)bfree_process_live_count());
        uart_puts("\n");
        return -11; /* EAGAIN */
    }
    if (!knl_current_task) {
        return -1;
    }
    slot = bfree_process_find_free_slot();
    if (slot < 0) {
        uart_puts("[FORK] EAGAIN no free slot\n");
        return -11; /* EAGAIN */
    }
    pt_idx = bfree_process_alloc_fork_pt();
    if (pt_idx < 0) {
        uart_puts("[FORK] EAGAIN no fork PT\n");
        return -11; /* EAGAIN */
    }

    __asm__ volatile("mov %%cr3, %0" : "=r"(live_cr3));
    parent = live_cr3;
    if (parent == &kernel_page_table) {
        uart_puts("[FORK] WARN: live CR3 was kernel; use TCB page_table_base\n");
        parent = (page_table_t *)knl_current_task->page_table_base;
        if (parent) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(parent) : "memory");
        }
    }
    if (!parent) {
        parent = (page_table_t *)knl_current_task->page_table_base;
    }
    if (!parent) {
        bfree_process_release_fork_pt(pt_idx);
        return -1;
    }
    uart_puts("[FORK] AS-copy parent_pt=");
    uart_puthex64((uint64_t)(uintptr_t)parent);
    uart_puts(" pt_idx=");
    uart_puthex64((uint64_t)(unsigned)pt_idx);
    uart_puts("\n");
    knl_current_task->page_table_base = parent;

    child = &g_fork_page_table[pt_idx];
    if (parent == child) {
        bfree_process_release_fork_pt(pt_idx);
        return -12; /* ENOMEM — cannot clone a table into itself */
    }
    if (!g_fork_pt_kernel_ready[pt_idx]) {
        vmm_clone_kernel_page_table(child);
        g_fork_pt_kernel_ready[pt_idx] = 1;
    } else {
        vmm_destroy_user_mappings(child);
    }
    if (vmm_clone_user_address_space(parent, child) != 0) {
        vmm_destroy_user_mappings(child);
        bfree_process_release_fork_pt(pt_idx);
        return -12; /* ENOMEM */
    }

    ch = &g_children[slot];
    ch->pid = g_next_pid++;
    if (ch->pid <= 1) {
        g_next_pid = 2;
        ch->pid = g_next_pid++;
    }
    ch->ppid = 1;
    ch->pgid = 1;
    ch->exit_status = 0;
    ch->exited_signal = 0;
    ch->stop_sig = 0;
    ch->stop_pending = 0;
    ch->parent_pt = parent;
    ch->child_pt = child;
    ch->fork_pt_idx = pt_idx;
    ch->coop_session = -1;
    ch->has_private_as = 1;
    ch->state = BFREE_PROC_RUNNING;
    /* AS-copy uses fork PT pool — do not mark g_child_pt_busy (vfork+exec). */
    g_active = slot;

    if (child_pid) {
        *child_pid = ch->pid;
    }
    return 0;
}

int bfree_process_child_active(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    /* Runnable only — STOPPED children are live but not scheduled. */
    return ch && bfree_process_is_runnable_state(ch->state);
}

void bfree_process_heal_focus_for_exit(void)
{
    int i;

    bfree_process_heal_active();
    if (g_active >= 0) {
        return;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (bfree_process_is_runnable_state(g_children[i].state)) {
            g_active = i;
            return;
        }
    }
}

int bfree_process_heal_vfork_exit_session(void)
{
    int i;

    bfree_process_heal_focus_for_exit();
    if (g_active >= 0 &&
        bfree_process_is_live_state(g_children[g_active].state) &&
        (g_children[g_active].parent_pt != 0 ||
         g_children[g_active].state == BFREE_PROC_VFORK)) {
        return 1;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (!bfree_process_is_live_state(g_children[i].state)) {
            continue;
        }
        if (g_children[i].parent_pt != 0 ||
            g_children[i].state == BFREE_PROC_VFORK) {
            g_active = i;
            return 1;
        }
    }
    return 0;
}

int bfree_process_pid_is_stopped(int pid)
{
    int slot = bfree_process_slot_of_pid(pid);

    if (slot < 0) {
        return 0;
    }
    return g_children[slot].state == BFREE_PROC_STOPPED;
}

void bfree_process_clear_stop_pending(int pid)
{
    int slot = bfree_process_slot_of_pid(pid);

    if (slot >= 0) {
        g_children[slot].stop_pending = 0;
    }
}

long bfree_process_stop_pid(int pid, int sig)
{
    int slot = bfree_process_slot_of_pid(pid);
    bfree_proc_child_t *ch;

    if (slot < 0) {
        return -3; /* ESRCH */
    }
    ch = &g_children[slot];
    if (ch->state != BFREE_PROC_VFORK && ch->state != BFREE_PROC_RUNNING &&
        ch->state != BFREE_PROC_STOPPED) {
        return -3;
    }
    if (sig <= 0) {
        sig = 20; /* SIGTSTP */
    }
    ch->state = BFREE_PROC_STOPPED;
    ch->stop_sig = sig & 0x7f;
    if (ch->stop_sig == 0) {
        ch->stop_sig = 20;
    }
    ch->stop_pending = 1;
    /* If we stopped the focused child, prefer another runnable focus. */
    if (g_active == slot) {
        int other = bfree_process_first_runnable_pid();

        g_active = -1;
        if (other > 0) {
            (void)bfree_process_select_pid(other);
        } else {
            g_active = slot; /* keep focus on stopped for cont/wait */
        }
    }
    return 0;
}

long bfree_process_cont_pid(int pid)
{
    int slot = bfree_process_slot_of_pid(pid);
    bfree_proc_child_t *ch;

    if (slot < 0) {
        return -3; /* ESRCH */
    }
    ch = &g_children[slot];
    if (ch->state != BFREE_PROC_STOPPED) {
        /* Already running / vfork — CONT is a no-op success. */
        if (ch->state == BFREE_PROC_RUNNING || ch->state == BFREE_PROC_VFORK) {
            return 0;
        }
        return -3;
    }
    ch->state = BFREE_PROC_RUNNING;
    ch->stop_pending = 0;
    ch->stop_sig = 0;
    g_active = slot;
    return 0;
}

int bfree_process_child_pid(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->pid : 0;
}

int bfree_process_child_has_private_as(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->has_private_as : 0;
}

int bfree_process_child_fork_pt_idx(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->fork_pt_idx : -1;
}

int bfree_process_child_coop_session(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->coop_session : -1;
}

void bfree_process_child_set_coop_session(int sess)
{
    bfree_proc_child_t *ch = bfree_process_active();

    if (ch) {
        ch->coop_session = sess;
    }
}

page_table_t *bfree_process_parent_pt(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->parent_pt : 0;
}

page_table_t *bfree_process_child_pt(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch ? ch->child_pt : 0;
}

long bfree_process_exec_commit_as(page_table_t **out_child_pt)
{
    page_table_t *parent;
    page_table_t *child;
    bfree_proc_child_t *ch = bfree_process_active();

    if (!ch || !knl_current_task) {
        return -38; /* ENOSYS */
    }
    parent = ch->parent_pt;
    if (!parent) {
        parent = (page_table_t *)knl_current_task->page_table_base;
    }
    if (!parent) {
        return -1;
    }

    child = ch->child_pt;
    /*
     * fork() already installed a private AS (pool entry). Drop its user
     * pages and reload the ELF into that same child_pt. vfork+exec builds a new
     * AS on g_child_page_table here.
     */
    if (ch->has_private_as && child != 0) {
        vmm_destroy_user_mappings(child);
    } else {
        child = &g_child_page_table;
        if (parent == child) {
            return -12; /* ENOMEM */
        }
        if (g_child_pt_busy) {
            /* Another child may hold g_child_page_table — refuse. */
            return -12; /* ENOMEM */
        }
        if (!g_child_pt_kernel_ready) {
            vmm_clone_kernel_page_table(child);
            g_child_pt_kernel_ready = 1;
        } else {
            vmm_destroy_user_mappings(child);
        }
        g_child_pt_busy = 1;
        ch->parent_pt = parent;
        ch->child_pt = child;
        ch->has_private_as = 1;
        ch->fork_pt_idx = -1;
    }
    ch->state = BFREE_PROC_RUNNING;

    if (out_child_pt) {
        *out_child_pt = child;
    }
    return 0;
}

void bfree_process_exec_activate_as(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    if (!ch || !ch->child_pt || !knl_current_task) {
        return;
    }
    knl_current_task->page_table_base = ch->child_pt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(ch->child_pt) : "memory");
}

void bfree_process_exit_restore_as(void)
{
    bfree_proc_child_t *ch = bfree_process_active();
    int pt_idx;

    if (!ch || !ch->has_private_as) {
        if (ch && ch->fork_pt_idx < 0) {
            g_child_pt_busy = 0;
        }
        return;
    }
    if (ch->parent_pt && knl_current_task) {
        knl_current_task->page_table_base = ch->parent_pt;
        __asm__ volatile("mov %0, %%cr3" :: "r"(ch->parent_pt) : "memory");
    }

    if (ch->child_pt) {
        vmm_destroy_user_mappings_keep(ch->child_pt, ch->parent_pt);
    }
    pt_idx = ch->fork_pt_idx;
    if (pt_idx >= 0) {
        bfree_process_release_fork_pt(pt_idx);
    } else {
        g_child_pt_busy = 0;
    }
    ch->child_pt = 0;
    ch->has_private_as = 0;
    ch->parent_pt = 0;
    ch->fork_pt_idx = -1;
}

static void bfree_process_exit_finish(bfree_proc_child_t *ch, int status, int signaled)
{
    int other;

    if (!ch) {
        g_active = -1;
        return;
    }
    bfree_process_exit_restore_as();
    ch->exit_status = status;
    ch->exited_signal = signaled ? 1 : 0;
    ch->state = BFREE_PROC_ZOMBIE;
    ch->coop_session = -1;
    g_active = -1;
    /* Focus any remaining live child so wait/yield can find it. */
    other = bfree_process_first_live_pid();
    if (other > 0) {
        (void)bfree_process_select_pid(other);
    }
}

void bfree_process_exit_child(int status)
{
    bfree_proc_child_t *ch = bfree_process_active();

    if (!ch) {
        g_active = -1;
        return;
    }
    bfree_process_exit_finish(ch, status & 0xff, 0);
}

void bfree_process_exit_child_signal(int sig)
{
    bfree_proc_child_t *ch = bfree_process_active();
    int st;

    if (!ch) {
        g_active = -1;
        return;
    }
    st = sig & 0x7f;
    if (st == 0) {
        st = 9;
    }
    bfree_process_exit_finish(ch, st, 1);
}

int bfree_process_getpgid(int pid)
{
    int i;

    if (pid == 0 || pid == 1) {
        return 1; /* shell session */
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_FREE && g_children[i].pid == pid) {
            return g_children[i].pgid;
        }
    }
    return -3; /* ESRCH */
}

int bfree_process_pgid_has_member(int pgid)
{
    int i;

    if (pgid <= 0) {
        return 0;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_FREE && g_children[i].pgid == pgid) {
            return 1;
        }
    }
    return 0;
}

long bfree_process_setpgid(int pid, int pgid)
{
    bfree_proc_child_t *ch;
    int i;

    if (pid < 0 || pgid < 0) {
        return -22; /* EINVAL */
    }
    if (pid == 0) {
        ch = bfree_process_active();
        if (!ch) {
            return 0;
        }
        pid = ch->pid;
    }
    if (pgid == 0) {
        pgid = pid;
    }
    if (pid == 1) {
        return 0;
    }
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_FREE && g_children[i].pid == pid) {
            g_children[i].pgid = pgid;
            return 0;
        }
    }
    ch = bfree_process_active();
    if (ch && ch->pid == pid) {
        ch->pgid = pgid;
        return 0;
    }
    return -3; /* ESRCH */
}

long bfree_process_wait4(long pid, int *status_out, int options)
{
    int i;
    int found = -1;
    int found_stop = -1;
    int want_pgid = 0;
    int want_untraced = ((options & BFREE_WUNTRACED) != 0);

    if (pid < -1) {
        want_pgid = (int)(-pid);
        pid = -1;
    }

    /* Prefer unreaped zombies. */
    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_ZOMBIE) {
            continue;
        }
        if (want_pgid != 0 && g_children[i].pgid != want_pgid) {
            continue;
        }
        if (pid != -1 && pid != (long)g_children[i].pid) {
            continue;
        }
        if (found < 0 || g_children[i].pid < g_children[found].pid) {
            found = i;
        }
    }
    /* H06: WUNTRACED — report STOPPED with pending notification. */
    if (found < 0 && want_untraced) {
        for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
            if (g_children[i].state != BFREE_PROC_STOPPED ||
                !g_children[i].stop_pending) {
                continue;
            }
            if (want_pgid != 0 && g_children[i].pgid != want_pgid) {
                continue;
            }
            if (pid != -1 && pid != (long)g_children[i].pid) {
                continue;
            }
            if (found_stop < 0 ||
                g_children[i].pid < g_children[found_stop].pid) {
                found_stop = i;
            }
        }
        if (found_stop >= 0) {
            if (status_out) {
                /* WIFSTOPPED: (sig << 8) | 0x7f */
                *status_out = ((g_children[found_stop].stop_sig & 0xff) << 8) | 0x7f;
            }
            g_children[found_stop].stop_pending = 0;
            return (long)g_children[found_stop].pid;
        }
    }
    if (found < 0) {
        int have_live = 0;

        for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
            if (g_children[i].state == BFREE_PROC_FREE ||
                g_children[i].state == BFREE_PROC_ZOMBIE) {
                continue;
            }
            if (want_pgid != 0 && g_children[i].pgid != want_pgid) {
                continue;
            }
            if (pid != -1 && pid != (long)g_children[i].pid) {
                continue;
            }
            have_live = 1;
            break;
        }
        if (!have_live) {
            return -10; /* ECHILD */
        }
        return 0;
    }
    if (status_out) {
        if (g_children[found].exited_signal) {
            *status_out = g_children[found].exit_status & 0x7f;
        } else {
            *status_out = g_children[found].exit_status << 8;
        }
    }
    {
        long reaped = (long)g_children[found].pid;
        int pt_idx = g_children[found].fork_pt_idx;

        /* Defensive: exit_restore_as usually freed the PT already; soft-reap
         * path also releases. Never leave g_fork_pt_used sticky after wait. */
        if (pt_idx >= 0) {
            bfree_process_release_fork_pt(pt_idx);
        }
        g_children[found].state = BFREE_PROC_FREE;
        g_children[found].pid = 0;
        g_children[found].exited_signal = 0;
        g_children[found].stop_sig = 0;
        g_children[found].stop_pending = 0;
        g_children[found].has_private_as = 0;
        g_children[found].child_pt = 0;
        g_children[found].parent_pt = 0;
        g_children[found].fork_pt_idx = -1;
        g_children[found].coop_session = -1;
        return reaped;
    }
}

int bfree_process_force_zombie_live(void)
{
    return bfree_process_force_zombie_except(0);
}

int bfree_process_force_zombie_except(int keep_pid)
{
    int i;
    int n = 0;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (!bfree_process_is_live_state(g_children[i].state)) {
            continue;
        }
        if (keep_pid > 0 && g_children[i].pid == keep_pid) {
            continue;
        }
        /*
         * Soft zombie: do not run exit_restore_as / vmm_destroy here.
         * Force-reaping from the parent's wait path can otherwise tear down
         * the wrong AS and #UD the caller (seen in curated waitid).
         */
        if (g_children[i].fork_pt_idx >= 0) {
            bfree_process_release_fork_pt(g_children[i].fork_pt_idx);
        }
        g_children[i].has_private_as = 0;
        g_children[i].child_pt = 0;
        g_children[i].parent_pt = 0;
        g_children[i].fork_pt_idx = -1;
        g_children[i].exit_status = 0;
        g_children[i].exited_signal = 0;
        g_children[i].stop_sig = 0;
        g_children[i].stop_pending = 0;
        g_children[i].coop_session = -1;
        g_children[i].state = BFREE_PROC_ZOMBIE;
        ++n;
    }
    g_active = -1;
    if (keep_pid > 0) {
        (void)bfree_process_select_pid(keep_pid);
    }
    return n;
}
