// userland_arm/net_api_testcases.c
// ネットワークAPI単体テストケース集
// 2025/12/27 新規作成

#include "../include_arm/net_api_arm.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../include_arm/types_arm.h"

void test_udp_build_and_parse() {
    printf("[TEST] UDPパケット生成・パース\n");
    uint8_t data[4] = {1,2,3,4};
    struct { uint8_t raw[1536]; } pkt;
    udp_build_packet(&pkt, 1000, 2000, data, 4);
    uint16_t src, dst, len; uint8_t buf[16];
    udp_parse_packet(&pkt, &src, &dst, buf, &len);
    printf("  src=%u dst=%u len=%u data=%02X%02X%02X%02X\n", src, dst, len, buf[0], buf[1], buf[2], buf[3]);
}

void test_arp_table() {
    printf("[TEST] ARPテーブル追加・検索\n");
    uint8_t mac[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    arp_table_add(0xC0A80002, mac);
    uint8_t out[6];
    int found = arp_table_lookup(0xC0A80002, out);
    printf("  検索結果: %s\n", found==0?"OK":"NG");
    if(found==0) printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", out[0],out[1],out[2],out[3],out[4],out[5]);
}

void test_udp_send_recv() {
    printf("[TEST] UDP送信・受信API(仮実装)\n");
    uint8_t data[8] = "TESTDATA";
    int ret = udp_send_arm(0xC0A80002, 2000, data, 8);
    printf("  udp_send_arm: %s\n", ret==0?"OK":"NG");
    // 受信は未実装のためNGが正常
    ret = udp_recv_arm(NULL, NULL, data, 8);
    printf("  udp_recv_arm: %s\n", ret<0?"NG(未実装)":"OK");
}

void test_ip_send_recv() {
    printf("[TEST] IP送信・受信API(仮実装)\n");
    uint8_t data[8] = "IPDATA";
    int ret = ip_send_arm(0xC0A80002, 17, data, 8);
    printf("  ip_send_arm: %s\n", ret==0?"OK":"NG");
    ret = ip_recv_arm(NULL, NULL, data, 8);
    printf("  ip_recv_arm: %s\n", ret<0?"NG(未実装)":"OK");
}

void test_udp_error_cases() {
    printf("[TEST] UDP異常系テスト\n");
    int ret = udp_send_arm(0, 0, NULL, 8);
    printf("  NULLデータ: %s\n", ret<0?"NG":"OK");
    uint8_t data[1600];
    ret = udp_send_arm(0xC0A80002, 2000, data, 1600);
    printf("  長さ超過: %s\n", ret<0?"NG":"OK");
}

int main(void) {
    printf("ネットワークAPI単体テスト開始\n");
    test_udp_build_and_parse();
    test_arp_table();
    test_udp_send_recv();
    test_ip_send_recv();
    test_udp_error_cases();

    // --- 負荷・多重・異常系テスト拡充 ---
    printf("[TEST] UDP大量送信負荷テスト\n");
    for(int i=0;i<1000;++i) {
        uint8_t data[8] = "LOADTEST";
        udp_send_arm(0xC0A80002, 2000, data, 8);
    }
    printf("  1000回送信完了\n");

    printf("[TEST] 多重ソケット作成テスト\n");
    int sockids[40];
    int count=0;
    for(int i=0;i<40;++i) {
        int id = net_socket(0);
        if(id>=0) { sockids[count++]=id; }
    }
    printf("  作成成功数: %d\n", count);

    printf("[TEST] NULL/異常値テスト\n");
    udp_send_arm(0,0,NULL,0);
    udp_send_arm(0xFFFFFFFF,65535,NULL,9999);

    printf("[TEST] 連続呼び出し・リソース枯渇テスト\n");
    for(int i=0;i<MAX_SOCKETS+10;++i) net_socket(1);

    printf("ネットワークAPI単体テスト終了\n");
    return 0;
}
