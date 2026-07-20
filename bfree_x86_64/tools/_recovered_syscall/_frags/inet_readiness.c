/* S3-02: readiness for pipe-backed AF_INET/AF_UNIX (fd bases 0x3A00/0x3900). */
static uint32_t bfree_guest_sock_ready_mask(int fd)
{
    int resolved = bfree_guest_fd_resolve(fd);
    int iidx;
    int uidx;
    int mag;
    int slot;
    uint32_t mask = 0;

    iidx = bfree_inet_from_fd(resolved);
    if (iidx >= 0) {
        if (g_inet_socks[iidx].listening) {
            if (g_inet_socks[iidx].accept_rd >= 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_inet_socks[iidx].connected && g_inet_socks[iidx].pipe_magic >= 0) {
            mag = g_inet_socks[iidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
                if (g_guest_pipes[slot].len > 0) {
                    mask |= (uint32_t)EPOLLIN;
                }
            }
        }
        return mask;
    }

    uidx = bfree_unix_from_fd(resolved);
    if (uidx >= 0) {
        if (g_unix_socks[uidx].listening) {
            if (g_unix_socks[uidx].accept_rd >= 0) {
                mask |= (uint32_t)EPOLLIN;
            }
            return mask;
        }
        if (g_unix_socks[uidx].connected && g_unix_socks[uidx].pipe_magic >= 0) {
            mag = g_unix_socks[uidx].pipe_magic;
            slot = bfree_guest_pipe_slot_from_magic(mag);
            if (slot >= 0 && g_guest_pipes[slot].used) {
                if (g_guest_pipes[slot].len < BFREE_GUEST_PIPE_BUF_SIZE) {
                    mask |= (uint32_t)EPOLLOUT;
                }
                if (g_guest_pipes[slot].len > 0) {
                    mask |= (uint32_t)EPOLLIN;
                }
            }
        }
    }
    return mask;
}

/* Soft-0 for apps that ignore sockopt failure; no NIC option store. */
static long sys_linux_setsockopt(long fd, long level, long optname, long optval, long optlen)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0;
}

static long sys_linux_getsockopt(long fd, long level, long optname, long optval, long optlen_ptr)
{
    (void)fd;
    (void)level;
    (void)optname;
    (void)optval;
    if (optlen_ptr != 0 && bfree_user_ptr_mapped(optlen_ptr)) {
        *(int *)(uintptr_t)optlen_ptr = 0;
    }
    return 0;
}

static long sys_linux_shutdown(long fd, long how)
{
    (void)fd;
    (void)how;
    return 0;
}

/* Real bind/connect addr for loopback inet; soft-0 length clamp. */
static long sys_linux_getsockname(long sockfd, long addr, long addrlen_ptr)
{
    int idx;
    int *alen;
    uint8_t *raw;
    uint16_t port_be;
    uint32_t addr_be;
    uint32_t a;
    int want;

    if (addr == 0 || addrlen_ptr == 0 || !bfree_user_ptr_mapped(addrlen_ptr)) {
        return -14;
    }
    alen = (int *)(uintptr_t)addrlen_ptr;
    want = *alen;
    if (want < 0) {
        return -22;
    }
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx < 0 || (!g_inet_socks[idx].bound && !g_inet_socks[idx].connected)) {
        return -88; /* ENOTSOCK / not bound */
    }
    if (want < 8 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    a = g_inet_socks[idx].addr;
    if (a == BFREE_INADDR_ANY) {
        a = BFREE_INADDR_LOOPBACK;
    }
    port_be = bfree_inet_ntohs(g_inet_socks[idx].port); /* host→be via same swap */
    addr_be = bfree_inet_ntohl(a);
    raw = (uint8_t *)(uintptr_t)addr;
    raw[0] = (uint8_t)(BFREE_LINUX_AF_INET & 0xff);
    raw[1] = (uint8_t)((BFREE_LINUX_AF_INET >> 8) & 0xff);
    raw[2] = (uint8_t)(port_be & 0xff);
    raw[3] = (uint8_t)((port_be >> 8) & 0xff);
    raw[4] = (uint8_t)(addr_be & 0xff);
    raw[5] = (uint8_t)((addr_be >> 8) & 0xff);
    raw[6] = (uint8_t)((addr_be >> 16) & 0xff);
    raw[7] = (uint8_t)((addr_be >> 24) & 0xff);
    *alen = 16;
    return 0;
}

static long sys_linux_getpeername(long sockfd, long addr, long addrlen_ptr)
{
    int idx;
    int *alen;
    uint8_t *raw;
    uint16_t port_be;
    uint32_t addr_be;
    int want;

    if (addr == 0 || addrlen_ptr == 0 || !bfree_user_ptr_mapped(addrlen_ptr)) {
        return -14;
    }
    alen = (int *)(uintptr_t)addrlen_ptr;
    want = *alen;
    if (want < 0) {
        return -22;
    }
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx < 0 || !g_inet_socks[idx].connected) {
        return -107; /* ENOTCONN */
    }
    if (want < 8 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    port_be = bfree_inet_ntohs(g_inet_socks[idx].port);
    addr_be = bfree_inet_ntohl(g_inet_socks[idx].addr == BFREE_INADDR_ANY
                                   ? BFREE_INADDR_LOOPBACK
                                   : g_inet_socks[idx].addr);
    raw = (uint8_t *)(uintptr_t)addr;
    raw[0] = (uint8_t)(BFREE_LINUX_AF_INET & 0xff);
    raw[1] = (uint8_t)((BFREE_LINUX_AF_INET >> 8) & 0xff);
    raw[2] = (uint8_t)(port_be & 0xff);
    raw[3] = (uint8_t)((port_be >> 8) & 0xff);
    raw[4] = (uint8_t)(addr_be & 0xff);
    raw[5] = (uint8_t)((addr_be >> 8) & 0xff);
    raw[6] = (uint8_t)((addr_be >> 16) & 0xff);
    raw[7] = (uint8_t)((addr_be >> 24) & 0xff);
    *alen = 16;
    return 0;
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