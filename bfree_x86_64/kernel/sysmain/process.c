#include "process.h"

#include "../include/tk/kernel.h"

extern TCB *knl_current_task;

static bfree_proc_child_t g_children[BFREE_PROC_MAX_CHILDREN];
static int g_active = -1; /* index of VFORK/RUNNING child, or -1 */
static int g_next_pid = 2;

/* Dedicated address space for the cooperative child after execve.
 * One private AS at a time (only one RUNNING child). */
static int g_child_pt_busy;
static int g_child_pt_kernel_ready;
/* Large BSS object last so clone memcpy cannot clobber the flags above. */
static page_table_t g_child_page_table __attribute__((aligned(4096), section(".bss.page_table")));

static bfree_proc_child_t *bfree_process_active(void)
{
    if (g_active < 0 || g_active >= BFREE_PROC_MAX_CHILDREN) {
        return 0;
    }
    return &g_children[g_active];
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
            g_children[i].state = BFREE_PROC_FREE;
            g_children[i].pid = 0;
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
        g_children[i].exit_status = 0;
        g_children[i].has_private_as = 0;
        g_children[i].parent_pt = 0;
        g_children[i].child_pt = 0;
    }
    g_active = -1;
    g_next_pid = 2;
    g_child_pt_busy = 0;
    g_child_pt_kernel_ready = 0;
}

long bfree_process_vfork_enter(int *child_pid)
{
    int slot;
    bfree_proc_child_t *ch;

    /* Only one cooperative live child (shared single task). */
    if (g_active >= 0) {
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
    ch->exit_status = 0;
    ch->has_private_as = 0;
    ch->parent_pt = knl_current_task ? (page_table_t *)knl_current_task->page_table_base : 0;
    ch->child_pt = 0;
    ch->state = BFREE_PROC_VFORK;
    g_active = slot;
    if (child_pid) {
        *child_pid = ch->pid;
    }
    return 0;
}

int bfree_process_child_active(void)
{
    bfree_proc_child_t *ch = bfree_process_active();

    return ch && (ch->state == BFREE_PROC_VFORK || ch->state == BFREE_PROC_RUNNING);
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
    if (g_child_pt_busy) {
        return -12; /* ENOMEM */
    }

    child = &g_child_page_table;
    /*
     * Only clone the kernel half once. Reading kernel_page_table spans past
     * 0x400000 in BSS; unmap_init must restore identity there so this works.
     */
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
    ch->state = BFREE_PROC_RUNNING;

    /*
     * Do NOT switch CR3 here. SYSCALL still uses the ring3 RSP; child_pt has no
     * user stack yet. load_elf maps into child_pt; syscall_entry.S activates
     * CR3 with the new user RSP (g_bfree_sysret_exec_cr3).
     */
    if (out_child_pt) {
        *out_child_pt = child;
    }
    return 0;
}

/* Activate child AS after ELF+stack are installed in child_pt. */
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

    if (!ch || !ch->has_private_as || !ch->parent_pt || !knl_current_task) {
        return;
    }

    knl_current_task->page_table_base = ch->parent_pt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(ch->parent_pt) : "memory");

    if (ch->child_pt) {
        vmm_destroy_user_mappings(ch->child_pt);
    }
    g_child_pt_busy = 0;
    ch->child_pt = 0;
    ch->has_private_as = 0;
    ch->parent_pt = 0;
}

void bfree_process_exit_child(int status)
{
    bfree_proc_child_t *ch = bfree_process_active();

    if (!ch) {
        return;
    }
    bfree_process_exit_restore_as();
    ch->exit_status = status & 0xff;
    ch->state = BFREE_PROC_ZOMBIE;
    g_active = -1; /* parent may vfork again; zombie remains until wait */
}

long bfree_process_wait4(long pid, int *status_out, int options)
{
    int wnohang = (options & 1) != 0;
    int i;
    int found = -1;

    for (i = 0; i < BFREE_PROC_MAX_CHILDREN; ++i) {
        if (g_children[i].state != BFREE_PROC_ZOMBIE) {
            continue;
        }
        if (pid != -1 && pid != (long)g_children[i].pid) {
            continue;
        }
        found = i;
        break;
    }
    if (found < 0) {
        return wnohang ? 0 : -10; /* ECHILD */
    }
    if (status_out) {
        *status_out = g_children[found].exit_status << 8;
    }
    {
        long reaped = (long)g_children[found].pid;
        g_children[found].state = BFREE_PROC_FREE;
        g_children[found].pid = 0;
        return reaped;
    }
}
