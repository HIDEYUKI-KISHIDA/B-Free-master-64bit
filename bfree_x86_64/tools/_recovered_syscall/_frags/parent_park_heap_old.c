    return 0; /* vfork: continue as child; parent resumes on child exit */
}

/* Snapshot parent heap/cwd at park time (AS-copy yield). */
static void bfree_guest_parent_park_heap(void)
{
    size_t i;

    g_guest_parent_parked_heap_next = g_guest_heap_next;
    g_guest_parent_parked_brk = g_guest_brk;
    for (i = 0; i < sizeof(g_guest_parent_parked_cwd); ++i) {
        g_guest_parent_parked_cwd[i] = g_guest_cwd[i];
        if (g_guest_cwd[i] == '\0') {
            break;
        }
    }
    g_guest_parent_parked_cwd[sizeof(g_guest_parent_parked_cwd) - 1U] = '\0';
    g_guest_parent_parked_heap_valid = 1;
}

static void bfree_guest_restore_parent_isol(int as_copy)
{
    size_t i;
    const char *cwd_src;
    uint64_t heap_src;
    uint64_t brk_src;

    if (as_copy && g_guest_parent_parked_heap_valid) {
        cwd_src = g_guest_parent_parked_cwd;
        heap_src = g_guest_parent_parked_heap_next;
        brk_src = g_guest_parent_parked_brk;
    } else {
        cwd_src = g_guest_fork_saved_cwd;
        heap_src = g_guest_fork_saved_heap_next;
        brk_src = g_guest_fork_saved_brk;
    }
    for (i = 0; i < sizeof(g_guest_cwd); ++i) {
        g_guest_cwd[i] = cwd_src[i];
        if (cwd_src[i] == '\0') {
            break;
        }
    }
    g_guest_cwd[sizeof(g_guest_cwd) - 1U] = '\0';
    g_guest_heap_next = heap_src;
    g_guest_brk = brk_src;
    g_guest_parent_parked_heap_valid = 0;
}

static long bfree_guest_exit_from_fork(long status)