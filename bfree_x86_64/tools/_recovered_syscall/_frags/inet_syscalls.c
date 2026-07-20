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