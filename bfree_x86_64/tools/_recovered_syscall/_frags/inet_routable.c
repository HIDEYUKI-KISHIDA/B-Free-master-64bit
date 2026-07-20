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