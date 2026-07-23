#include "net_tcp_min.h"

#include <stddef.h>
#include <stdint.h>

#include "../../../kernel/net_ipv4.h"

extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern void uart_puts(const char *s);
extern void net_runtime_poll(void);

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_ST_CLOSED 0
#define TCP_ST_SYN_SENT 1
#define TCP_ST_ESTABLISHED 2
#define TCP_ST_CLOSED_DONE 3

#define TCP_RX_MAX 2048

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off; /* high nibble = words */
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    int used;
    int state;
    int syn_on_wire;
    uint32_t rip;
    uint16_t rport;
    uint16_t lport;
    uint32_t iss;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    int rx_len;
    uint8_t rx[TCP_RX_MAX];
} tcp_pcb_t;

static tcp_pcb_t g_pcb[BFREE_TCP_MAX];
static uint16_t g_ephemeral = 41000;
static uint32_t g_iss_seed = 0x1000u;

static uint16_t htons16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint32_t htonl32(uint32_t v)
{
    return ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) |
           ((v >> 8) & 0xff00u) | (v >> 24);
}

/* RFC1071 over network-order bytes (IPV4() stores wire bytes in a LE uint32). */
static uint32_t csum_add_bytes(uint32_t sum, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; i += 2)
        sum += (uint32_t)(((uint16_t)p[i] << 8) | p[i + 1]);
    if (n & 1)
        sum += (uint32_t)((uint16_t)p[n - 1] << 8);
    return sum;
}

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, const uint8_t *tcp, size_t tcp_len)
{
    uint32_t sum = 0;
    uint8_t ipb[4];
    uint8_t ph[4];

    ipb[0] = (uint8_t)(src & 0xffu);
    ipb[1] = (uint8_t)((src >> 8) & 0xffu);
    ipb[2] = (uint8_t)((src >> 16) & 0xffu);
    ipb[3] = (uint8_t)((src >> 24) & 0xffu);
    sum = csum_add_bytes(sum, ipb, 4);
    ipb[0] = (uint8_t)(dst & 0xffu);
    ipb[1] = (uint8_t)((dst >> 8) & 0xffu);
    ipb[2] = (uint8_t)((dst >> 16) & 0xffu);
    ipb[3] = (uint8_t)((dst >> 24) & 0xffu);
    sum = csum_add_bytes(sum, ipb, 4);
    ph[0] = 0;
    ph[1] = 6; /* TCP */
    ph[2] = (uint8_t)((tcp_len >> 8) & 0xffu);
    ph[3] = (uint8_t)(tcp_len & 0xffu);
    sum = csum_add_bytes(sum, ph, 4);
    sum = csum_add_bytes(sum, tcp, tcp_len);
    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    /* Header field is native-stored htons; invert then store via assignment. */
    return htons16((uint16_t)~sum);
}

static int tcp_tx(tcp_pcb_t *p, uint8_t flags, const uint8_t *payload, size_t plen)
{
    uint8_t buf[64 + 1400];
    tcp_hdr_t *h;
    size_t tcp_len;
    int rc;
    uint32_t lip = ipv4_get_local_ip();

    if (sizeof(tcp_hdr_t) + plen > sizeof(buf))
        return -1;
    memset(buf, 0, sizeof(tcp_hdr_t));
    h = (tcp_hdr_t *)buf;
    h->src_port = htons16(p->lport);
    h->dst_port = htons16(p->rport);
    h->seq = htonl32(p->snd_nxt);
    h->ack = htonl32(p->rcv_nxt);
    h->data_off = (uint8_t)(5u << 4);
    h->flags = flags;
    h->window = htons16(8192);
    h->checksum = 0;
    h->urgent = 0;
    if (plen && payload)
        memcpy(buf + sizeof(tcp_hdr_t), payload, plen);
    tcp_len = sizeof(tcp_hdr_t) + plen;
    h->checksum = tcp_checksum(lip, p->rip, buf, tcp_len);

    rc = ipv4_send(p->rip, 6, buf, tcp_len);
    if (rc == NET_SEND_PENDING) {
        /* ARP in flight — caller should poll and retry. */
        return -11;
    }
    if (rc != NET_SEND_OK)
        return -101;
    if (flags & TCP_SYN) {
        if (!p->syn_on_wire) {
            p->snd_nxt += 1;
            p->syn_on_wire = 1;
        }
    }
    if (plen)
        p->snd_nxt += (uint32_t)plen;
    if (flags & TCP_FIN)
        p->snd_nxt += 1;
    return (int)plen;
}

void tcp_min_init(void)
{
    memset(g_pcb, 0, sizeof(g_pcb));
}

void tcp_min_poll(void)
{
    /* RX path is driven by net_runtime_poll → ipv4_input → tcp_min_input. */
}

void tcp_min_input(uint32_t src_ip, const uint8_t *pkt, size_t len)
{
    const tcp_hdr_t *h;
    tcp_pcb_t *p = 0;
    size_t hdr_len;
    size_t plen;
    const uint8_t *payload;
    uint16_t sport, dport;
    uint32_t seq, ack;
    int i;

    if (len < sizeof(tcp_hdr_t))
        return;
    h = (const tcp_hdr_t *)pkt;
    hdr_len = (size_t)(h->data_off >> 4) * 4u;
    if (hdr_len < sizeof(tcp_hdr_t) || hdr_len > len)
        return;
    sport = htons16(h->src_port);
    dport = htons16(h->dst_port);
    seq = htonl32(h->seq);
    ack = htonl32(h->ack);
    payload = pkt + hdr_len;
    plen = len - hdr_len;

    for (i = 0; i < BFREE_TCP_MAX; ++i) {
        if (g_pcb[i].used && g_pcb[i].lport == dport &&
            g_pcb[i].rport == sport && g_pcb[i].rip == src_ip) {
            p = &g_pcb[i];
            break;
        }
    }
    if (!p) {
        /* SYN-SENT match by local port only (peer port known). */
        for (i = 0; i < BFREE_TCP_MAX; ++i) {
            if (g_pcb[i].used && g_pcb[i].state == TCP_ST_SYN_SENT &&
                g_pcb[i].lport == dport && g_pcb[i].rip == src_ip &&
                g_pcb[i].rport == sport) {
                p = &g_pcb[i];
                break;
            }
        }
    }
    if (!p)
        return;

    uart_puts("[TCP] rx flags=");
    {
        extern void uart_puthex64(uint64_t v);
        uart_puthex64((uint64_t)h->flags);
        uart_puts("\n");
    }

    if (h->flags & TCP_RST) {
        p->state = TCP_ST_CLOSED_DONE;
        return;
    }

    if (p->state == TCP_ST_SYN_SENT) {
        if ((h->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            p->rcv_nxt = seq + 1;
            p->snd_nxt = ack; /* our ISS+1 should equal ack */
            (void)tcp_tx(p, TCP_ACK, 0, 0);
            p->state = TCP_ST_ESTABLISHED;
            uart_puts("[TCP] established\n");
        }
        return;
    }

    if (p->state != TCP_ST_ESTABLISHED)
        return;

    if (plen > 0) {
        if (seq == p->rcv_nxt) {
            size_t n = plen;
            if (n > (size_t)(TCP_RX_MAX - p->rx_len))
                n = (size_t)(TCP_RX_MAX - p->rx_len);
            if (n) {
                memcpy(p->rx + p->rx_len, payload, n);
                p->rx_len += (int)n;
                p->rcv_nxt += (uint32_t)n;
            }
        }
        (void)tcp_tx(p, TCP_ACK, 0, 0);
    }
    if (h->flags & TCP_FIN) {
        p->rcv_nxt += 1;
        (void)tcp_tx(p, TCP_ACK, 0, 0);
    }
}

int tcp_min_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port)
{
    int i;
    int id = -1;
    tcp_pcb_t *p;
    int spins;
    int rc = -11;

    for (i = 0; i < BFREE_TCP_MAX; ++i) {
        if (!g_pcb[i].used) {
            id = i;
            break;
        }
    }
    if (id < 0)
        return -24;
    p = &g_pcb[id];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->state = TCP_ST_SYN_SENT;
    p->rip = dst_ip;
    p->rport = dst_port;
    p->lport = src_port ? src_port : (g_ephemeral++);
    if (g_ephemeral < 41000)
        g_ephemeral = 41000;
    p->iss = g_iss_seed += 0x1111u;
    p->snd_nxt = p->iss;
    p->rcv_nxt = 0;
    p->syn_on_wire = 0;

    /* Best-effort first SYN; ARP reply often needs a later syscall (QEMU TCG). */
    for (spins = 0; spins < 64; ++spins) {
        rc = tcp_tx(p, TCP_SYN, 0, 0);
        if (rc != -11)
            break;
        net_runtime_poll();
    }
    /* Non-blocking: handshake + SYN retransmit via tcp_min_pump across syscalls. */
    return id;
}

int tcp_min_pump(int pcb)
{
    tcp_pcb_t *p;
    if (pcb < 0 || pcb >= BFREE_TCP_MAX || !g_pcb[pcb].used)
        return -9;
    p = &g_pcb[pcb];
    net_runtime_poll();
    if (p->state == TCP_ST_ESTABLISHED)
        return 0;
    if (p->state == TCP_ST_CLOSED_DONE) {
        p->used = 0;
        return -111;
    }
    if (p->state == TCP_ST_SYN_SENT) {
        static uint32_t syn_kick;
        int try_syn = !p->syn_on_wire || ((++syn_kick & 0x1fu) == 0);
        if (try_syn) {
            uint32_t nxt = p->snd_nxt;
            p->snd_nxt = p->iss;
            (void)tcp_tx(p, TCP_SYN, 0, 0);
            if (p->syn_on_wire)
                p->snd_nxt = p->iss + 1;
            else
                p->snd_nxt = nxt;
        }
        net_runtime_poll();
    }
    return -115; /* EINPROGRESS */
}

int tcp_min_send(int pcb, const uint8_t *data, size_t len)
{
    tcp_pcb_t *p;
    int rc;
    int spins;
    int pr;

    if (pcb < 0 || pcb >= BFREE_TCP_MAX || !g_pcb[pcb].used)
        return -9;
    p = &g_pcb[pcb];
    for (spins = 0; spins < 512; ++spins) {
        pr = tcp_min_pump(pcb);
        if (pr == 0)
            break;
        if (pr != -115)
            return pr;
    }
    if (p->state != TCP_ST_ESTABLISHED)
        return -115;
    if (!data || len == 0)
        return 0;
    if (len > 1200)
        len = 1200;
    for (spins = 0; spins < 512; ++spins) {
        rc = tcp_tx(p, (uint8_t)(TCP_PSH | TCP_ACK), data, len);
        if (rc != -11)
            break;
        net_runtime_poll();
    }
    return rc;
}

int tcp_min_recv(int pcb, uint8_t *buf, size_t len)
{
    tcp_pcb_t *p;
    size_t n;
    int pr;
    int spins;

    if (pcb < 0 || pcb >= BFREE_TCP_MAX || !g_pcb[pcb].used)
        return -9;
    p = &g_pcb[pcb];
    for (spins = 0; spins < 8; ++spins) {
        pr = tcp_min_pump(pcb);
        if (pr == 0)
            break;
        if (pr != -115)
            return pr;
    }
    if (p->state != TCP_ST_ESTABLISHED && p->rx_len <= 0)
        return -115;
    if (p->rx_len <= 0)
        return -11;
    n = (size_t)p->rx_len;
    if (n > len)
        n = len;
    memcpy(buf, p->rx, n);
    if (n < (size_t)p->rx_len) {
        size_t rem = (size_t)p->rx_len - n;
        size_t j;
        for (j = 0; j < rem; ++j)
            p->rx[j] = p->rx[n + j];
    }
    p->rx_len -= (int)n;
    return (int)n;
}

void tcp_min_close(int pcb)
{
    if (pcb < 0 || pcb >= BFREE_TCP_MAX || !g_pcb[pcb].used)
        return;
    if (g_pcb[pcb].state == TCP_ST_ESTABLISHED)
        (void)tcp_tx(&g_pcb[pcb], (uint8_t)(TCP_FIN | TCP_ACK), 0, 0);
    g_pcb[pcb].used = 0;
    g_pcb[pcb].state = TCP_ST_CLOSED;
}
