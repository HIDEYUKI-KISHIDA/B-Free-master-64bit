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