// kernel_arm/net_extra_arm.c
// ARM/64ビット 高度ネットワーク機能（ICMP/DHCP/DNS等）雛形
// 2025/12/27 新規作成

#include "../include_arm/net_api_arm.h"
#include <stdint.h>

// ICMP送信
int icmp_send_arm(uint32_t dst_ip, uint8_t type, uint8_t code, const void *data, uint16_t len) {
    // TODO: ICMPパケット生成・送信
    return -1;
}

// ICMP受信
int icmp_recv_arm(uint32_t *src_ip, uint8_t *type, uint8_t *code, void *buf, uint16_t buflen) {
    // TODO: ICMPパケット受信・パース
    return -1;
}

// DHCPクライアント雛形
int dhcp_request_arm(void) {
    // TODO: DHCP DISCOVER/SEND/RECV
    return -1;
}

// DNSクエリ雛形
int dns_query_arm(const char *hostname, uint32_t *out_ip) {
    // TODO: DNSリクエスト・レスポンス処理
    return -1;
}
