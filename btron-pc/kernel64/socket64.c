
// socket64.c - 64ビット用 ネットワークAPI雛形（本物雰囲気実装）
#include "../include64/types.h"
#include <stddef.h>
#define MAX_SOCKET64 16

typedef enum {
    SOCK_STATE_UNUSED = 0,
    SOCK_STATE_CREATED,
    SOCK_STATE_BOUND,
    SOCK_STATE_LISTENING,
    SOCK_STATE_CONNECTED,
    SOCK_STATE_CLOSED
} sock_state64_t;


typedef struct {
    int used;
    int domain;
    int type;
    int protocol;
    sock_state64_t state;
    u64 local_addr;
    u64 remote_addr;
    int local_port;
    int remote_port;
    int backlog;
    char rxbuf[256];
    int rxlen;
    char txbuf[256];
    int txlen;
} socket_entry64_t;

static socket_entry64_t socket_table64[MAX_SOCKET64];

int socket64(int domain, int type, int protocol) {
    for (int i = 0; i < MAX_SOCKET64; i++) {
        if (!socket_table64[i].used) {
            socket_table64[i].used = 1;
            socket_table64[i].domain = domain;
            socket_table64[i].type = type;
            socket_table64[i].protocol = protocol;
            socket_table64[i].state = SOCK_STATE_CREATED;
            socket_table64[i].local_addr = 0;
            socket_table64[i].remote_addr = 0;
            socket_table64[i].backlog = 0;
            return i;
        }
    }
    return -1;
}

int bind64(int sockfd, const void *addr, int addrlen) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    socket_table64[sockfd].state = SOCK_STATE_BOUND;
    // 雰囲気: アドレス・ポートをセット
    socket_table64[sockfd].local_addr = (u64)addr;
    socket_table64[sockfd].local_port = 10000 + sockfd; // 雰囲気
    return 0;
}

int listen64(int sockfd, int backlog) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    if (socket_table64[sockfd].state != SOCK_STATE_BOUND) return -1;
    socket_table64[sockfd].state = SOCK_STATE_LISTENING;
    socket_table64[sockfd].backlog = backlog;
    return 0;
}

int accept64(int sockfd, void *addr, int *addrlen) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    if (socket_table64[sockfd].state != SOCK_STATE_LISTENING) return -1;
    // 雰囲気: 新しいソケットを割り当ててCONNECTEDに
    for (int i = 0; i < MAX_SOCKET64; i++) {
        if (!socket_table64[i].used) {
            socket_table64[i].used = 1;
            socket_table64[i].domain = socket_table64[sockfd].domain;
            socket_table64[i].type = socket_table64[sockfd].type;
            socket_table64[i].protocol = socket_table64[sockfd].protocol;
            socket_table64[i].state = SOCK_STATE_CONNECTED;
            socket_table64[i].local_addr = socket_table64[sockfd].local_addr;
            socket_table64[i].remote_addr = 0x1234; // 雰囲気
            if (addr && addrlen) {
                *addrlen = sizeof(u64);
                *(u64*)addr = socket_table64[i].remote_addr;
            }
            return i;
        }
    }
    return -1;
}

int connect64(int sockfd, const void *addr, int addrlen) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    socket_table64[sockfd].state = SOCK_STATE_CONNECTED;
    socket_table64[sockfd].remote_addr = (u64)addr;
    socket_table64[sockfd].remote_port = 20000 + sockfd; // 雰囲気
    // NICデバイス連携雰囲気
    extern int device_request64(const char*, int, void*);
    device_request64("nic", 1, NULL); // NIC起動雰囲気
    return 0;
}

int send64(int sockfd, const void *buf, int len, int flags) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    if (socket_table64[sockfd].state != SOCK_STATE_CONNECTED) return -1;
    // 雰囲気: パケットバッファにコピー
    int n = len < 255 ? len : 255;
    memcpy(socket_table64[sockfd].txbuf, buf, n);
    socket_table64[sockfd].txlen = n;
    // NICデバイス連携雰囲気
    extern int device_request64(const char*, int, void*);
    device_request64("nic", 2, socket_table64[sockfd].txbuf); // NIC送信雰囲気
    return n;
}

int recv64(int sockfd, void *buf, int len, int flags) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    if (socket_table64[sockfd].state != SOCK_STATE_CONNECTED) return -1;
    // 雰囲気: パケットバッファからコピー
    int n = socket_table64[sockfd].rxlen < len ? socket_table64[sockfd].rxlen : len;
    memcpy(buf, socket_table64[sockfd].rxbuf, n);
    socket_table64[sockfd].rxlen = 0;
    return n;
}

int close64(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_SOCKET64 || !socket_table64[sockfd].used) return -1;
    socket_table64[sockfd].used = 0;
    socket_table64[sockfd].state = SOCK_STATE_CLOSED;
    return 0;
}
