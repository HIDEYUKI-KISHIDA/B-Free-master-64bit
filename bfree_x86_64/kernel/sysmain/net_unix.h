#ifndef BFREE_NET_UNIX_H
#define BFREE_NET_UNIX_H

struct bfree_sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

int bfree_socket(int domain, int type, int protocol);
int bfree_bind(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_connect(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_sendto(int sockfd, const void *buf, unsigned int len, int flags,
                 const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_recvfrom(int sockfd, void *buf, unsigned int len, int flags,
                   struct bfree_sockaddr_un *addr, unsigned int *addrlen);
int bfree_socketpair(int domain, int type, int protocol, int sv[2]);
void bfree_net_reset(void);

#endif
