// userland_arm/net_api_sample.c
// ARM/64ビット用ネットワークAPI利用サンプル
// 2025/12/27 新規作成

#include "../include_arm/net_api_arm.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    printf("ネットワークAPIサンプル開始\n");
    // UDPパケット生成・パース例
    uint8_t data[8] = "TESTDATA";
    uint8_t buf[1500];
    uint16_t src, dst, len;
    struct { uint8_t raw[1536]; } pkt;
    udp_build_packet(&pkt, 1234, 5678, data, 8);
    udp_parse_packet(&pkt, &src, &dst, buf, &len);
    printf("UDP src=%u dst=%u len=%u data=%.8s\n", src, dst, len, buf);
    // ARPテーブル追加・検索例
    uint8_t mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    arp_table_add(0xC0A80001, mac);
    uint8_t mac_out[6];
    if (arp_table_lookup(0xC0A80001, mac_out) == 0)
        printf("ARP検索成功: %02X:%02X:%02X:%02X:%02X:%02X\n", mac_out[0],mac_out[1],mac_out[2],mac_out[3],mac_out[4],mac_out[5]);
    // IPテーブル追加・検索例
    ip_table_add(0xC0A80001);
    if (ip_table_lookup(0xC0A80001) == 0)
        printf("IP検索成功\n");
    // イーサネットフレーム生成・パース例
    struct { uint8_t raw[1600]; } frame;
    ethernet_build_frame(&frame, mac, mac_out, 0x0800, data, 8);
    uint8_t dmac[6], smac[6], payload[1500];
    uint16_t ethertype, plen;
    ethernet_parse_frame(&frame, dmac, smac, &ethertype, payload, &plen);
    printf("ETH type=0x%04X plen=%u data=%.8s\n", ethertype, plen, payload);
    printf("ネットワークAPIサンプル終了\n");
    return 0;
}
