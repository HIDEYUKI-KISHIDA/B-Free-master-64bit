#include "epoll.h"

#include <errno.h>
#include <string.h>

#define BFREE_EPOLL_MAX 8
#define BFREE_EPOLL_FDS 16

struct bfree_epoll {
	int in_use;
	int fds[BFREE_EPOLL_FDS];
	uint32_t events[BFREE_EPOLL_FDS];
	uint64_t data[BFREE_EPOLL_FDS];
	int nfds;
};

static struct bfree_epoll bfree_epolls[BFREE_EPOLL_MAX];
static int next_epfd = 200;

void bfree_epoll_reset(void)
{
	memset(bfree_epolls, 0, sizeof(bfree_epolls));
	next_epfd = 200;
}

int bfree_epoll_create1(int flags)
{
	int i;

	(void)flags;
	for (i = 0; i < BFREE_EPOLL_MAX; i++) {
		if (!bfree_epolls[i].in_use) {
			memset(&bfree_epolls[i], 0, sizeof(bfree_epolls[i]));
			bfree_epolls[i].in_use = 1;
			return next_epfd + i;
		}
	}
	return -EMFILE;
}

static struct bfree_epoll *ep_from_fd(int epfd)
{
	int idx = epfd - 200;

	if (idx < 0 || idx >= BFREE_EPOLL_MAX || !bfree_epolls[idx].in_use)
		return NULL;
	return &bfree_epolls[idx];
}

int bfree_epoll_ctl(int epfd, int op, int fd, struct bfree_epoll_event *event)
{
	struct bfree_epoll *ep = ep_from_fd(epfd);
	int i;

	if (ep == NULL)
		return -EBADF;
	if (op == BFREE_EPOLL_CTL_ADD) {
		if (ep->nfds >= BFREE_EPOLL_FDS)
			return -ENOMEM;
		if (event == NULL)
			return -EFAULT;
		ep->fds[ep->nfds] = fd;
		ep->events[ep->nfds] = event->events;
		ep->data[ep->nfds] = event->data;
		ep->nfds++;
		return 0;
	}
	if (op == BFREE_EPOLL_CTL_DEL) {
		for (i = 0; i < ep->nfds; i++) {
			if (ep->fds[i] == fd) {
				ep->fds[i] = ep->fds[ep->nfds - 1];
				ep->events[i] = ep->events[ep->nfds - 1];
				ep->data[i] = ep->data[ep->nfds - 1];
				ep->nfds--;
				return 0;
			}
		}
		return -ENOENT;
	}
	if (op == BFREE_EPOLL_CTL_MOD) {
		for (i = 0; i < ep->nfds; i++) {
			if (ep->fds[i] == fd) {
				if (event == NULL)
					return -EFAULT;
				ep->events[i] = event->events;
				ep->data[i] = event->data;
				return 0;
			}
		}
		return -ENOENT;
	}
	return -EINVAL;
}

int bfree_epoll_wait(struct bfree_proc_mgr *mgr, struct bfree_fs *fs, int epfd,
		     struct bfree_epoll_event *events, int maxevents,
		     int timeout)
{
	struct bfree_epoll *ep = ep_from_fd(epfd);
	struct bfree_pollfd pfds[BFREE_EPOLL_FDS];
	int i;
	int n;
	int out = 0;

	(void)timeout;
	if (ep == NULL)
		return -EBADF;
	if (events == NULL || maxevents <= 0)
		return -EINVAL;
	for (i = 0; i < ep->nfds; i++) {
		pfds[i].fd = ep->fds[i];
		pfds[i].events = (short)ep->events[i];
		pfds[i].revents = 0;
	}
	n = bfree_poll(mgr, fs, pfds, (unsigned int)ep->nfds, 0);
	if (n < 0)
		return n;
	for (i = 0; i < ep->nfds && out < maxevents; i++) {
		if (pfds[i].revents) {
			events[out].events = (uint32_t)pfds[i].revents;
			events[out].data = ep->data[i];
			out++;
		}
	}
	return out;
}
