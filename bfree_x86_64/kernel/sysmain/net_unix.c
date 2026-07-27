#include "net_unix.h"

#include <errno.h>
#include <string.h>

#define BFREE_AF_UNIX 1
#define BFREE_SOCK_STREAM 1
#define BFREE_SOCK_DGRAM 2

#define BFREE_SOCK_MAX 8
#define BFREE_SOCK_BUF 256

enum bfree_sock_state {
    BFREE_SOCK_FREE = 0,
    BFREE_SOCK_CREATED,
    BFREE_SOCK_BOUND,
    BFREE_SOCK_CONNECTED,
    BFREE_SOCK_LISTEN,
};

struct bfree_unix_sock {
    int in_use;
    int type;
    enum bfree_sock_state state;
    char path[64];
    int peer;
    int backlog;
    int pending;
    unsigned char buf[BFREE_SOCK_BUF];
    int buf_len;
};

static struct bfree_unix_sock bfree_socks[BFREE_SOCK_MAX];

static int bfree_sock_alloc(void)
{
    for (int i = 0; i < BFREE_SOCK_MAX; i++) {
        if (!bfree_socks[i].in_use) {
            memset(&bfree_socks[i], 0, sizeof(bfree_socks[i]));
            bfree_socks[i].in_use = 1;
            bfree_socks[i].peer = -1;
            return i;
        }
    }
    return -ENFILE;
}

static struct bfree_unix_sock *bfree_sock_get(int fd)
{
    if (fd < 0 || fd >= BFREE_SOCK_MAX || !bfree_socks[fd].in_use)
        return 0;
    return &bfree_socks[fd];
}

static int bfree_sock_find_bound(const char *path)
{
    for (int i = 0; i < BFREE_SOCK_MAX; i++) {
        if (bfree_socks[i].in_use &&
            (bfree_socks[i].state == BFREE_SOCK_BOUND ||
             bfree_socks[i].state == BFREE_SOCK_LISTEN) &&
            strcmp(bfree_socks[i].path, path) == 0)
            return i;
    }
    return -1;
}

int bfree_socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != BFREE_AF_UNIX)
        return -EAFNOSUPPORT;
    if (type != BFREE_SOCK_STREAM && type != BFREE_SOCK_DGRAM)
        return -EPROTOTYPE;
    int fd = bfree_sock_alloc();
    if (fd < 0)
        return fd;
    bfree_socks[fd].type = type;
    bfree_socks[fd].state = BFREE_SOCK_CREATED;
    return fd;
}

int bfree_bind(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (!addr || addrlen < 3)
        return -EINVAL;
    if (addr->sun_family != BFREE_AF_UNIX)
        return -EINVAL;
    if (bfree_sock_find_bound(addr->sun_path) >= 0)
        return -EADDRINUSE;
    strncpy(s->path, addr->sun_path, sizeof(s->path) - 1);
    s->path[sizeof(s->path) - 1] = '\0';
    s->state = BFREE_SOCK_BOUND;
    return 0;
}

int bfree_connect(int sockfd, const struct bfree_sockaddr_un *addr, unsigned int addrlen)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (!addr || addrlen < 3)
        return -EINVAL;
    if (addr->sun_family != BFREE_AF_UNIX)
        return -EINVAL;
    int peer = bfree_sock_find_bound(addr->sun_path);
    if (peer < 0)
        return -ECONNREFUSED;
    if (peer == sockfd)
        return -EINVAL;
    struct bfree_unix_sock *p = &bfree_socks[peer];
    if (p->state == BFREE_SOCK_LISTEN) {
        if (p->pending >= 0)
            return -EAGAIN;
        p->pending = sockfd;
        s->peer = peer;
        s->state = BFREE_SOCK_CONNECTED;
        strncpy(s->path, addr->sun_path, sizeof(s->path) - 1);
        return 0;
    }
    if (s->type == BFREE_SOCK_STREAM) {
        if (p->peer >= 0)
            return -EISCONN;
        s->peer = peer;
        p->peer = sockfd;
        s->state = BFREE_SOCK_CONNECTED;
        p->state = BFREE_SOCK_CONNECTED;
    } else {
        s->peer = peer;
        s->state = BFREE_SOCK_CONNECTED;
    }
    return 0;
}

int bfree_listen(int sockfd, int backlog)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (s->state != BFREE_SOCK_BOUND && s->state != BFREE_SOCK_LISTEN)
        return -EINVAL;
    s->backlog = backlog > 0 ? backlog : 1;
    s->state = BFREE_SOCK_LISTEN;
    s->pending = -1;
    return 0;
}

int bfree_accept(int sockfd, struct bfree_sockaddr_un *addr, unsigned int *addrlen)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    int neu;
    struct bfree_unix_sock *ns;

    if (!s)
        return -EBADF;
    if (s->state != BFREE_SOCK_LISTEN)
        return -EINVAL;
    if (s->pending < 0)
        return -EAGAIN;
    neu = bfree_sock_alloc();
    if (neu < 0)
        return neu;
    ns = &bfree_socks[neu];
    ns->type = s->type;
    ns->state = BFREE_SOCK_CONNECTED;
    ns->peer = s->pending;
    bfree_socks[s->pending].peer = neu;
    bfree_socks[s->pending].state = BFREE_SOCK_CONNECTED;
    if (addr && addrlen) {
        memset(addr, 0, sizeof(*addr));
        addr->sun_family = BFREE_AF_UNIX;
        strncpy(addr->sun_path, bfree_socks[s->pending].path,
                sizeof(addr->sun_path) - 1);
        *addrlen = (unsigned int)(2 + strlen(addr->sun_path) + 1);
    }
    s->pending = -1;
    return neu;
}

int bfree_shutdown(int sockfd, int how)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    (void)how;
    if (!s)
        return -EBADF;
    s->state = BFREE_SOCK_CREATED;
    s->peer = -1;
    s->buf_len = 0;
    return 0;
}

int bfree_getsockname(int sockfd, struct bfree_sockaddr_un *addr,
                      unsigned int *addrlen)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (!addr || !addrlen)
        return -EINVAL;
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = BFREE_AF_UNIX;
    strncpy(addr->sun_path, s->path, sizeof(addr->sun_path) - 1);
    *addrlen = (unsigned int)(2 + strlen(addr->sun_path) + 1);
    return 0;
}

int bfree_getpeername(int sockfd, struct bfree_sockaddr_un *addr,
                      unsigned int *addrlen)
{
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (s->peer < 0)
        return -ENOTCONN;
    return bfree_getsockname(s->peer, addr, addrlen);
}

int bfree_setsockopt(int sockfd, int level, int optname, const void *optval,
                     unsigned int optlen)
{
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    if (!bfree_sock_get(sockfd))
        return -EBADF;
    return 0;
}

int bfree_getsockopt(int sockfd, int level, int optname, void *optval,
                     unsigned int *optlen)
{
    (void)level;
    (void)optname;
    if (!bfree_sock_get(sockfd))
        return -EBADF;
    if (optval && optlen && *optlen >= sizeof(int)) {
        *(int *)optval = 0;
        *optlen = sizeof(int);
    }
    return 0;
}

int bfree_sendmsg(int sockfd, const void *msg, int flags)
{
    const struct {
        void *iov_base;
        unsigned long iov_len;
    } *iov = msg;

    if (!iov)
        return -EINVAL;
    return bfree_sendto(sockfd, iov->iov_base, (unsigned int)iov->iov_len,
                        flags, NULL, 0);
}

int bfree_recvmsg(int sockfd, void *msg, int flags)
{
    struct {
        void *iov_base;
        unsigned long iov_len;
    } *iov = msg;

    if (!iov)
        return -EINVAL;
    return bfree_recvfrom(sockfd, iov->iov_base, (unsigned int)iov->iov_len,
                          flags, NULL, NULL);
}

int bfree_sendto(int sockfd, const void *buf, unsigned int len, int flags,
                 const struct bfree_sockaddr_un *addr, unsigned int addrlen)
{
    (void)flags;
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (!buf || len == 0)
        return -EINVAL;
    int peer = s->peer;
    if (addr) {
        if (addrlen < 3 || addr->sun_family != BFREE_AF_UNIX)
            return -EINVAL;
        peer = bfree_sock_find_bound(addr->sun_path);
        if (peer < 0)
            return -ECONNREFUSED;
    }
    if (peer < 0)
        return -ENOTCONN;
    struct bfree_unix_sock *p = &bfree_socks[peer];
    unsigned int n = len;
    if (n > BFREE_SOCK_BUF)
        n = BFREE_SOCK_BUF;
    if (s->type == BFREE_SOCK_STREAM && p->buf_len > 0)
        return -EAGAIN;
    memcpy(p->buf, buf, n);
    p->buf_len = (int)n;
    return (int)n;
}

int bfree_recvfrom(int sockfd, void *buf, unsigned int len, int flags,
                   struct bfree_sockaddr_un *addr, unsigned int *addrlen)
{
    (void)flags;
    struct bfree_unix_sock *s = bfree_sock_get(sockfd);
    if (!s)
        return -EBADF;
    if (!buf || len == 0)
        return -EINVAL;
    if (s->buf_len == 0)
        return -EAGAIN;
    unsigned int n = (unsigned int)s->buf_len;
    if (n > len)
        n = len;
    memcpy(buf, s->buf, n);
    s->buf_len = 0;
    if (addr && addrlen) {
        memset(addr, 0, sizeof(*addr));
        addr->sun_family = BFREE_AF_UNIX;
        if (s->peer >= 0)
            strncpy(addr->sun_path, bfree_socks[s->peer].path, sizeof(addr->sun_path) - 1);
        *addrlen = (unsigned int)(2 + strlen(addr->sun_path) + 1);
    }
    return (int)n;
}

int bfree_socketpair(int domain, int type, int protocol, int sv[2])
{
    (void)protocol;
    if (!sv)
        return -EINVAL;
    if (domain != BFREE_AF_UNIX)
        return -EAFNOSUPPORT;
    if (type != BFREE_SOCK_STREAM)
        return -EPROTONOSUPPORT;
    int a = bfree_sock_alloc();
    if (a < 0)
        return a;
    int b = bfree_sock_alloc();
    if (b < 0) {
        bfree_socks[a].in_use = 0;
        return b;
    }
    bfree_socks[a].type = BFREE_SOCK_STREAM;
    bfree_socks[b].type = BFREE_SOCK_STREAM;
    bfree_socks[a].peer = b;
    bfree_socks[b].peer = a;
    bfree_socks[a].state = BFREE_SOCK_CONNECTED;
    bfree_socks[b].state = BFREE_SOCK_CONNECTED;
    sv[0] = a;
    sv[1] = b;
    return 0;
}

void bfree_net_reset(void)
{
    memset(bfree_socks, 0, sizeof(bfree_socks));
}
