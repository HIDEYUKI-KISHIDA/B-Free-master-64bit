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