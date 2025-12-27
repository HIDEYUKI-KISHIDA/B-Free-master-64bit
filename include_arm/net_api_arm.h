// include_arm/net_api_arm.h
// ARM/64ビット・btron-pc共通ネットワークAPI宣言・構造体集約
// 2025/12/27 新規作成

#ifndef NET_API_ARM_H
#define NET_API_ARM_H

#include <stdint.h>

// Ether
#define ETH_ALEN      6
#define ETH_HLEN      14
#define ETH_DATA_LEN  1500
#define ETH_FRAME_LEN 1514
#define ETH_P_IP      0x0800
#define ETH_P_ARP     0x0806
#define ETH_P_IPV6    0x86DD

struct ethhdr_arm {
    unsigned char  h_dest[ETH_ALEN];
    unsigned char  h_source[ETH_ALEN];
    unsigned short h_proto;
};

typedef struct {
    struct ethhdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} ethernet_frame_t;

// ARP
struct arphdr_arm {
    unsigned short ar_hrd;
    unsigned short ar_pro;
    unsigned char  ar_hln;
    unsigned char  ar_pln;
    unsigned short ar_op;
};

typedef struct {
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
    int      valid;
} arp_entry_t;

// IP
#define IPVERSION 4
struct iphdr_arm {
    unsigned char ip_hl:4, ip_v:4;
    unsigned char ip_tos;
    unsigned short ip_len;
    unsigned short ip_id;
    unsigned short ip_off;
    unsigned char ip_ttl;
    unsigned char ip_p;
    unsigned short ip_csum;
    unsigned long ip_src, ip_dst;
};

typedef struct {
    struct iphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} ip_packet_t;

// UDP
struct udphdr_arm {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};

typedef struct {
    struct udphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} udp_packet_t;

// TCP
struct tcphdr_arm {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
};

typedef struct {
    struct tcphdr_arm header;
    uint8_t payload[ETH_DATA_LEN];
} tcp_packet_t;

// API宣言例（必要に応じて追加）
int udp_build_packet(udp_packet_t *pkt, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len);
int udp_parse_packet(const udp_packet_t *pkt, uint16_t *src_port, uint16_t *dst_port, uint8_t *data, uint16_t *len);
int tcp_build_packet(tcp_packet_t *pkt, uint16_t src_port, uint16_t dst_port, const uint8_t *data, uint16_t len);
int tcp_parse_packet(const tcp_packet_t *pkt, uint16_t *src_port, uint16_t *dst_port, uint8_t *data, uint16_t *len);

#endif // NET_API_ARM_H
