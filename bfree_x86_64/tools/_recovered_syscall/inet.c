/* Recovered AF_INET (+ dual-path unix) socket layer from f40d0181 / fa9a295b / 97982f27.
 * Extends tools/_unix_coop_block.c (AF_UNIX only). Direct edits — NOT in _patch_*.py.
 * NOTE: BFREE_INET_FD_BASE 0x3A00 collides with _patch_pty_stage2.py PTY_MASTER_BASE —
 * reconcile FD bases when merging.
 */


#define BFREE_SYSRET_COOP_SWITCH ((long)-4092)
#define BFREE_LINUX_AF_UNIX 1
#define BFREE_LINUX_AF_INET 2
#define BFREE_UNIX_SLOTS 8
#define BFREE_UNIX_FD_BASE 0x3900
#define BFREE_INET_SLOTS 8
#define BFREE_INET_FD_BASE 0x3A00
#define BFREE_INADDR_LOOPBACK 0x7f000001U /* 127.0.0.1 host order */
#define BFREE_INADDR_ANY 0U

typedef struct {
    int used;
    int listening;
    int connected;
    int accept_rd;
    int pipe_magic;
    char path[96];
} bfree_unix_sock_t;

typedef struct {
    int used;
    int listening;
    int connected;
    int bound;
    uint32_t addr; /* host order */
    uint16_t port; /* host order */
    int accept_rd;
    int pipe_magic;
} bfree_inet_sock_t;

static bfree_unix_sock_t g_unix_socks[BFREE_UNIX_SLOTS];
static bfree_inet_sock_t g_inet_socks[BFREE_INET_SLOTS];
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static uint8_t g_fd_cloexec_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static uint8_t g_fd_cloexec_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static uint64_t g_coop_child_rcx, g_coop_child_r11, g_coop_child_rsp;
static uint64_t g_coop_child_rbx, g_coop_child_rbp, g_coop_child_r12;
static uint64_t g_coop_child_r13, g_coop_child_r14, g_coop_child_r15, g_coop_child_rdx;

static int bfree_inet_from_fd(int fd)
{
    int idx;
    if (fd < (int)BFREE_INET_FD_BASE || fd >= (int)BFREE_INET_FD_BASE + BFREE_INET_SLOTS) {
        return -1;
    }
    idx = fd - (int)BFREE_INET_FD_BASE;
    return g_inet_socks[idx].used ? idx : -1;
}

static uint16_t bfree_inet_ntohs(uint16_t x)
{
    return (uint16_t)(((x & 0xffU) << 8) | ((x >> 8) & 0xffU));
}

static uint32_t bfree_inet_ntohl(uint32_t x)
{
    return ((x & 0xffU) << 24) | ((x & 0xff00U) << 8) |
           ((x >> 8) & 0xff00U) | ((x >> 24) & 0xffU);
}

static int bfree_inet_parse_sockaddr(long addr, long addrlen, uint32_t *out_addr, uint16_t *out_port)
{
    const uint8_t *raw;
    uint16_t family;
    uint16_t port_be;
    uint32_t addr_be;

    if (addrlen < 8 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    family = (uint16_t)(raw[0] | (raw[1] << 8));
    if (family != (uint16_t)BFREE_LINUX_AF_INET) {
        return -97; /* EAFNOSUPPORT */
    }
    port_be = (uint16_t)(raw[2] | (raw[3] << 8));
    addr_be = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) |
              ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    if (out_port) {
        *out_port = bfree_inet_ntohs(port_be);
    }
    if (out_addr) {
        *out_addr = bfree_inet_ntohl(addr_be);
    }
    return 0;
}

static int bfree_inet_is_loopback(uint32_t addr)
{
    return addr == BFREE_INADDR_LOOPBACK || addr == BFREE_INADDR_ANY;
}

static long sys_linux_socket(long domain, long type, long protocol)
{
    int i;
    (void)type;
    (void)protocol;
    if (domain == BFREE_LINUX_AF_INET) {
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (!g_inet_socks[i].used) {
                g_inet_socks[i].used = 1;
                g_inet_socks[i].listening = 0;
                g_inet_socks[i].connected = 0;
                g_inet_socks[i].bound = 0;
                g_inet_socks[i].addr = BFREE_INADDR_ANY;
                g_inet_socks[i].port = 0;
                g_inet_socks[i].accept_rd = -1;
                g_inet_socks[i].pipe_magic = -1;
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

static int bfree_inet_is_guest_routable(uint32_t addr)
{
    if (addr == BFREE_INADDR_ANY) {
        return 1;
    }
    if ((addr & 0xFF000000u) == 0x7F000000u) {
        return 1; /* 127.0.0.0/8 */
    }
    if ((addr & 0xFFFFFF00u) == 0x0A000200u) {
        return 1; /* 10.0.2.0/24 — QEMU user/slirp guest LAN */
    }
    return 0;
}

/* Collapse guest-local binds/connects onto loopback pipe listeners. */
static uint32_t bfree_inet_match_addr(uint32_t addr)
{
    if (addr == BFREE_INADDR_ANY) {
        return BFREE_INADDR_LOOPBACK;
    }
    if ((addr & 0xFF000000u) == 0x7F000000u) {
        return BFREE_INADDR_LOOPBACK;
    }
    if ((addr & 0xFFFFFF00u) == 0x0A000200u) {
        return BFREE_INADDR_LOOPBACK;
    }
    return addr;
}

static int bfree_inet_listener_matches(uint32_t bound_addr, uint32_t peer_addr)
{
    uint32_t b = bfree_inet_match_addr(bound_addr);
    uint32_t p = bfree_inet_match_addr(peer_addr);

    if (bound_addr == BFREE_INADDR_ANY) {
        return 1;
    }
    return b == p;
}

static int bfree_inet_is_loopback(uint32_t addr)
{
    return bfree_inet_is_guest_routable(addr);
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

static long sys_linux_bind(long sockfd, long addr, long addrlen)
{
    int idx, i;
    const uint8_t *raw;
    size_t n;
    uint32_t in_addr;
    uint16_t in_port;
    long perr;

    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        perr = bfree_inet_parse_sockaddr(addr, addrlen, &in_addr, &in_port);
        if (perr != 0) {
            return perr;
        }
        if (!bfree_inet_is_loopback(in_addr)) {
            return -101; /* ENETUNREACH — only loopback without NIC */
        }
        if (in_port == 0) {
            /* Ephemeral: pick unused 40000+idx */
            in_port = (uint16_t)(40000 + idx);
        }
        for (i = 0; i < BFREE_INET_SLOTS; ++i) {
            if (i != idx && g_inet_socks[i].used && g_inet_socks[i].bound &&
                g_inet_socks[i].port == in_port &&
                (g_inet_socks[i].addr == in_addr ||
                 g_inet_socks[i].addr == BFREE_INADDR_ANY ||
                 in_addr == BFREE_INADDR_ANY)) {
                return -98; /* EADDRINUSE */
            }
        }
        g_inet_socks[idx].addr = in_addr;
        g_inet_socks[idx].port = in_port;
        g_inet_socks[idx].bound = 1;
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (addrlen < 4 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    n = 0;
    while (n + 2U < (size_t)addrlen && n + 1U < sizeof(g_unix_socks[idx].path) &&
           raw[2 + n] != 0) {
        g_unix_socks[idx].path[n] = (char)raw[2 + n];
        ++n;
    }
    g_unix_socks[idx].path[n] = '\0';
    if (n == 0) {
        return -22;
    }
    for (i = 0; i < BFREE_UNIX_SLOTS; ++i) {
        if (i != idx && g_unix_socks[i].used && g_unix_socks[i].path[0] &&
            strcmp(g_unix_socks[i].path, g_unix_socks[idx].path) == 0) {
            return -98;
        }
    }
    return 0;
}

static long sys_linux_listen(long sockfd, long backlog)
{
    int idx;
    (void)backlog;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (!g_inet_socks[idx].bound) {
            return -22;
        }
        g_inet_socks[idx].listening = 1;
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (g_unix_socks[idx].path[0] == '\0') {
        return -22;
    }
    g_unix_socks[idx].listening = 1;
    return 0;
}

static long sys_linux_connect(long sockfd, long addr, long addrlen)
{
    int idx, li, i, slot = -1, rd, wr;
    const uint8_t *raw;
    char path[96];
    size_t n;
    uint32_t in_addr;
    uint16_t in_port;
    long perr;

    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        perr = bfree_inet_parse_sockaddr(addr, addrlen, &in_addr, &in_port);
        if (perr != 0) {
            return perr;
        }
        if (!bfree_inet_is_loopback(in_addr)) {
            return -101; /* ENETUNREACH — no NIC / non-loopback */
        }
        for (li = 0; li < BFREE_INET_SLOTS; ++li) {
            if (g_inet_socks[li].used && g_inet_socks[li].listening &&
                g_inet_socks[li].port == in_port &&
                (g_inet_socks[li].addr == BFREE_INADDR_ANY ||
                 g_inet_socks[li].addr == in_addr ||
                 in_addr == BFREE_INADDR_LOOPBACK)) {
                break;
            }
        }
        if (li >= BFREE_INET_SLOTS) {
            return -111; /* ECONNREFUSED — no loopback listener */
        }
        if (g_inet_socks[li].accept_rd >= 0) {
            return -11; /* EAGAIN — one pending accept */
        }
        for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
            if (!g_guest_pipes[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            return -24;
        }
        g_guest_pipes[slot].used = 1;
        g_guest_pipes[slot].rd_open = 1;
        g_guest_pipes[slot].wr_open = 1;
        g_guest_pipes[slot].len = 0;
        g_guest_pipes[slot].nonblock = 0;
        rd = bfree_guest_pipe_magic_fd(slot, 0);
        wr = bfree_guest_pipe_magic_fd(slot, 1);
        g_inet_socks[idx].connected = 1;
        g_inet_socks[idx].pipe_magic = wr;
        g_inet_socks[idx].addr = in_addr;
        g_inet_socks[idx].port = in_port;
        g_inet_socks[li].accept_rd = rd;
        return 0;
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (addrlen < 4 || addr == 0 || !bfree_user_ptr_mapped(addr)) {
        return -14;
    }
    raw = (const uint8_t *)(uintptr_t)addr;
    n = 0;
    while (n + 2U < (size_t)addrlen && n + 1U < sizeof(path) && raw[2 + n] != 0) {
        path[n] = (char)raw[2 + n];
        ++n;
    }
    path[n] = '\0';
    for (li = 0; li < BFREE_UNIX_SLOTS; ++li) {
        if (g_unix_socks[li].used && g_unix_socks[li].listening &&
            strcmp(g_unix_socks[li].path, path) == 0) {
            break;
        }
    }
    if (li >= BFREE_UNIX_SLOTS) {
        return -111;
    }
    if (g_unix_socks[li].accept_rd >= 0) {
        return -11;
    }
    for (i = 0; i < BFREE_GUEST_PIPE_SLOTS; ++i) {
        if (!g_guest_pipes[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -24;
    }
    g_guest_pipes[slot].used = 1;
    g_guest_pipes[slot].rd_open = 1;
    g_guest_pipes[slot].wr_open = 1;
    g_guest_pipes[slot].len = 0;
    g_guest_pipes[slot].nonblock = 0;
    rd = bfree_guest_pipe_magic_fd(slot, 0);
    wr = bfree_guest_pipe_magic_fd(slot, 1);
    g_unix_socks[idx].connected = 1;
    g_unix_socks[idx].pipe_magic = wr;
    g_unix_socks[li].accept_rd = rd;
    return 0;
}

static long sys_linux_accept(long sockfd, long addr, long addrlen)
{
    int idx, rd;
    (void)addr;
    (void)addrlen;
    sockfd = bfree_guest_fd_resolve((int)sockfd);
    idx = bfree_inet_from_fd((int)sockfd);
    if (idx >= 0) {
        if (!g_inet_socks[idx].listening) {
            return -22;
        }
        if (g_inet_socks[idx].accept_rd < 0) {
            return -11;
        }
        rd = g_inet_socks[idx].accept_rd;
        g_inet_socks[idx].accept_rd = -1;
        return bfree_guest_fd_publish(rd);
    }
    idx = bfree_unix_from_fd((int)sockfd);
    if (idx < 0) {
        return -88;
    }
    if (!g_unix_socks[idx].listening) {
        return -22;
    }
    if (g_unix_socks[idx].accept_rd < 0) {
        return -11;
    }
    rd = g_unix_socks[idx].accept_rd;
    g_unix_socks[idx].accept_rd = -1;
    return bfree_guest_fd_publish(rd);
}

static long sys_linux_sendto(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    (void)flags;
    (void)addr;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_inet_socks[idx].pipe_magic, buf, len);
    }
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        return sys_linux_write(g_unix_socks[idx].pipe_magic, buf, len);
    }
    return sys_linux_write(fd, buf, len);
}

static long sys_linux_recvfrom(long fd, long buf, long len, long flags, long addr, long addrlen)
{
    int idx;
    (void)flags;
    (void)addr;
    (void)addrlen;
    fd = bfree_guest_fd_resolve((int)fd);
    idx = bfree_inet_from_fd((int)fd);
    if (idx >= 0 && g_inet_socks[idx].connected && g_inet_socks[idx].pipe_magic >= 0) {
        int mag = g_inet_socks[idx].pipe_magic;
        if (bfree_guest_pipe_is_wr_magic(mag)) {
            mag = mag - 1;
        }
        return sys_linux_read(mag, buf, len);
    }
    idx = bfree_unix_from_fd((int)fd);
    if (idx >= 0 && g_unix_socks[idx].connected && g_unix_socks[idx].pipe_magic >= 0) {
        /* Client sendto uses wr; client recv should use rd of same slot. */
        int mag = g_unix_socks[idx].pipe_magic;
        if (bfree_guest_pipe_is_wr_magic(mag)) {
            mag = mag - 1;
        }
        return sys_linux_read(mag, buf, len);
    }
    return sys_linux_read(fd, buf, len);
}

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

/* unix_from_fd / related (may already exist after _patch_unix_coop.py) */
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
