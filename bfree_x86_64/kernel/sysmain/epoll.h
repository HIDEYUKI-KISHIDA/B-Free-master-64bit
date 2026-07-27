#ifndef BFREE_EPOLL_H
#define BFREE_EPOLL_H

#include "fs_ofd.h"
#include "process.h"

#include <stdint.h>

#define BFREE_EPOLL_CTL_ADD 1
#define BFREE_EPOLL_CTL_DEL 2
#define BFREE_EPOLL_CTL_MOD 3

struct bfree_epoll_event {
	uint32_t events;
	uint64_t data;
};

int bfree_epoll_create1(int flags);
int bfree_epoll_ctl(int epfd, int op, int fd, struct bfree_epoll_event *event);
int bfree_epoll_wait(struct bfree_proc_mgr *mgr, struct bfree_fs *fs, int epfd,
		     struct bfree_epoll_event *events, int maxevents,
		     int timeout);
void bfree_epoll_reset(void);

#endif
