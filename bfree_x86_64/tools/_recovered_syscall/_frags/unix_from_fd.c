static int bfree_unix_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_UNIX_FD_BASE || fd >= (int)BFREE_UNIX_FD_BASE + BFREE_UNIX_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_UNIX_FD_BASE;
    return g_unix_socks[idx].used ? idx : -1;
}

static int bfree_inet_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_INET_FD_BASE || fd >= (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_INET_FD_BASE;
    return g_inet_socks[idx] ? idx : -1;
}

static long sys_linux_socket(long domain, long type, long protocol)
{
    int i;
    (void)type;
    (void)protocol;
    if (domain == BFREE_LINUX_AF_INET) {
        /* Stub AF_INET fd: connect fails with ENETUNREACH (no Stage3 net stack). */
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (!g_inet_socks[i]) {
                g_inet_socks[i] = 1;
                return bfree_guest_fd_publish((int)BFREE_INET_FD_BASE + i);
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
            return bfree_guest_fd_publish((int)BFREE_UNIX_FD_BASE + i);
        }
    }
    return -24;
}