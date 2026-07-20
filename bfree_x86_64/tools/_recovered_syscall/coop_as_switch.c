/* bfree_coop_as_switch_to + save_child_user (cf433436). Also inlined in as_copy.c. */

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

/* H02: AS-copy coop must flip CR3 with the logical parent/child side. */
static void bfree_coop_as_switch_to(int side)
{
    page_table_t *pt;

    if (!knl_current_task) {
        return;
    }
    if (!bfree_process_child_has_private_as()) {
        return; /* vfork / shared AS — nothing to flip */
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
}

static void bfree_coop_arm_parent_resume(void)
