// userland_arm/net_test_arm.c
// ネットワーク管理動作テスト用サンプル
// 2025/12/27 新規作成

#include "../../btron-pc/include_arm/types_arm.h"
#include <stdio.h>
#include <string.h>

extern void net_init(void);
extern int net_socket(int type);
extern int net_send(int sockfd, const void *buf, uint32_t size);
extern int net_recv(int sockfd, void *buf, uint32_t size);
extern int net_close(int sockfd);

int main(void) {
    printf("ネットワークテスト開始\n");
    net_init();
    int sockfd = net_socket(0); // UDP
    if (sockfd >= 0) {
        char sendbuf[] = "Hello ARM Net!";
        net_send(sockfd, sendbuf, strlen(sendbuf));
        char recvbuf[64] = {0};
        int n = net_recv(sockfd, recvbuf, sizeof(recvbuf)-1);
        if (n > 0) printf("受信: %s\n", recvbuf);
        net_close(sockfd);
    } else {
        printf("ソケット作成失敗\n");
    }
    printf("ネットワークテスト終了\n");
    return 0;
}
