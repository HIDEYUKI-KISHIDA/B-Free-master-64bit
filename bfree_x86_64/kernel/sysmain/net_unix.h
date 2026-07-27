#ifndef BFREE_NET_UNIX_H
#define BFREE_NET_UNIX_H

struct bfree_sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

int bfree_socket(int domain, int type, int protocol);
int bfree_bind(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_connect(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_listen(int sockfd, int backlog);
int bfree_accept(int sockfd, struct bfree_sockaddr_un *addr, unsigned int *addrlen);
int bfree_shutdown(int sockfd, int how);
int bfree_getsockname(int sockfd, struct bfree_sockaddr_un *addr, unsigned int *addrlen);
int bfree_getpeername(int sockfd, struct bfree_sockaddr_un *addr, unsigned int *addrlen);
int bfree_setsockopt(int sockfd, int level, int optname, const void *optval,
		     unsigned int optlen);
int bfree_getsockopt(int sockfd, int level, int optname, void *optval,
		     unsigned int *optlen);
int bfree_sendto(int sockfd, const void *buf, unsigned int len, int flags,
                 const struct bfree_sockaddr_un *addr, unsigned int addrlen);
int bfree_recvfrom(int sockfd, void *buf, unsigned int len, int flags,
                   struct bfree_sockaddr_un *addr, unsigned int *addrlen);
int bfree_sendmsg(int sockfd, const void *msg, int flags);
int bfree_recvmsg(int sockfd, void *msg, int flags);
int bfree_socketpair(int domain, int type, int protocol, int sv[2]);
void bfree_net_reset(void);

#endif
