static int bfree_inet_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_INET_FD_BASE || fd >= (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_INET_FD_BASE;
    return g_inet_socks[idx].used ? idx : -1;
}

static void bfree_inet_sock_release(int resolved)
{
    int iidx = bfree_inet_from_fd(resolved);
    if (iidx < 0) {
        return;
    }
    g_inet_socks[iidx].used = 0;
    g_inet_socks[iidx].listening = 0;
    g_inet_socks[iidx].connected = 0;
    g_inet_socks[iidx].bound = 0;
    g_inet_socks[iidx].accept_rd = -1;
    g_inet_socks[iidx].pipe_magic = -1;
}