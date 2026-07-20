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

/* Park live kernel heap/cwd into the parent or child AS-copy slot. */
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

/* Parent → child: park parent; load child park or birth (fork-enter) heap. */
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

/* Child → parent: park child; load parent park. */
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