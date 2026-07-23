#include <stddef.h>
#include <stdint.h>

#include "../../../kernel/net_arp.h"
#include "../../../kernel/net_icmp.h"
#include "../../../kernel/net_ipv4.h"
#include "../../../kernel/net_udp.h"

#include "../../x86_64/kernel/netdrv.h"
#include "devnet0_runtime.h"

void uart_puts(const char *s);
void uart_puthex64(uint64_t val);

#define ETHERTYPE_IPV4 0x0800U
#define ETHERTYPE_ARP 0x0806U
#define NET_RUNTIME_PKT_MAXLEN 1514U

typedef struct {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

static uint16_t net_runtime_ntoh16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

#define IPV4(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

void arp_set_mac(const uint8_t *mac);

void net_runtime_init(void)
{
    uart_puts("[NET] runtime init\n");
    netdrv_init();
    uart_puts("[NET] backend kind=");
    uart_puthex64((uint64_t)netdrv_backend_kind());
    uart_puts("\n");
    register_devnet0();
    
    uint8_t my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    arp_init();
    arp_set_mac(my_mac);
    ipv4_init();
    udp_init();
    icmp_init();
#if ENABLE_RUNTIME_NET
    {
        extern void tcp_min_init(void);
        tcp_min_init();
    }
#endif
    
    // Set static IP: 10.0.2.15 (QEMU default)
    ipv4_set_local_ip(IPV4(10, 0, 2, 15));
    ipv4_set_subnet_mask(IPV4(255, 255, 255, 0));
    ipv4_set_router_ip(IPV4(10, 0, 2, 2));

    extern void arp_cache_insert(uint32_t ip_addr, const uint8_t mac[6]);
    uint8_t qemu_router_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
    arp_cache_insert(IPV4(10, 0, 2, 2), qemu_router_mac);
    /* guestfwd targets on 10.0.2/24 (slirp); seed so TCP SYN is not stuck on ARP. */
    arp_cache_insert(IPV4(10, 0, 2, 100), qemu_router_mac);

    /* Gratuitous ARP announce (broadcast reply) so slirp learns our MAC. */
    {
        extern int netdrv_send(const void *buf, unsigned int len);
        extern uint8_t local_mac[6];
        uint8_t ga[42];
        uint32_t lip = IPV4(10, 0, 2, 15);
        int i;
        for (i = 0; i < 6; ++i)
            ga[i] = 0xff;
        for (i = 0; i < 6; ++i)
            ga[6 + i] = local_mac[i];
        ga[12] = 0x08;
        ga[13] = 0x06;
        ga[14] = 0x00;
        ga[15] = 0x01;
        ga[16] = 0x08;
        ga[17] = 0x00;
        ga[18] = 6;
        ga[19] = 4;
        ga[20] = 0x00;
        ga[21] = 0x02; /* reply */
        for (i = 0; i < 6; ++i)
            ga[22 + i] = local_mac[i];
        ga[28] = (uint8_t)(lip & 0xff);
        ga[29] = (uint8_t)((lip >> 8) & 0xff);
        ga[30] = (uint8_t)((lip >> 16) & 0xff);
        ga[31] = (uint8_t)((lip >> 24) & 0xff);
        for (i = 0; i < 6; ++i)
            ga[32 + i] = 0xff;
        ga[38] = ga[28];
        ga[39] = ga[29];
        ga[40] = ga[30];
        ga[41] = ga[31];
        (void)netdrv_send(ga, sizeof(ga));
    }
}

void net_runtime_poll(void)
{
    uint8_t pkt[NET_RUNTIME_PKT_MAXLEN];
    int packet_len;
    static uint32_t logged_short_frames;
    static uint32_t logged_arp_frames;
    static uint32_t logged_ipv4_frames;
    static int reentering;

    if (reentering)
        return;
    reentering = 1;

    {
        extern void netdrv_rx_kick(void);
        netdrv_rx_kick();
    }
    arp_tick();

    for (;;) {
        const eth_hdr_t *eth;
        uint16_t ether_type;

        packet_len = netdrv_recv(pkt, sizeof(pkt));
        if (packet_len <= 0)
            break;
        if ((size_t)packet_len < sizeof(eth_hdr_t)) {
            if (logged_short_frames < 4U) {
                uart_puts("[NET] short frame len=");
                uart_puthex64((uint64_t)(uint32_t)packet_len);
                uart_puts("\n");
                logged_short_frames += 1U;
            }
            continue;
        }

        eth = (const eth_hdr_t *)pkt;
        ether_type = net_runtime_ntoh16(eth->type);
        if (ether_type == ETHERTYPE_ARP) {
            if (logged_arp_frames < 8U) {
                uart_puts("[NET] rx ARP len=");
                uart_puthex64((uint64_t)(uint32_t)packet_len);
                uart_puts("\n");
                logged_arp_frames += 1U;
            }
            arp_input(pkt, (size_t)packet_len);
            continue;
        }
        if (ether_type == ETHERTYPE_IPV4) {
            if (logged_ipv4_frames < 8U) {
                uart_puts("[NET] rx IPv4 len=");
                uart_puthex64((uint64_t)(uint32_t)packet_len);
                uart_puts("\n");
                logged_ipv4_frames += 1U;
            }
            ipv4_input(pkt + sizeof(eth_hdr_t), (size_t)packet_len - sizeof(eth_hdr_t));
        }
    }
    reentering = 0;
}