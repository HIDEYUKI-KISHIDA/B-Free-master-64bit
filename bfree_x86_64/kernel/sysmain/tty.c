#include "tty.h"

#include <errno.h>

static int guest_tty_fg_pgrp;
static int guest_tty_sid;

static struct bfree_proc *find_pid(struct bfree_proc_mgr *mgr, int pid)
{
	int i;

	if (pid == 0) {
		struct bfree_proc *self = bfree_proc_current(mgr);

		return self;
	}
	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state != BFREE_PROC_FREE &&
		    mgr->procs[i].pid == pid)
			return &mgr->procs[i];
	}
	return NULL;
}

int bfree_setsid(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *self;

	self = bfree_proc_current(mgr);
	if (self == NULL)
		return -ESRCH;
	if (self->sid != 0 && self->sid == self->pid)
		return -EPERM;
	self->sid = self->pid;
	self->pgid = self->pid;
	guest_tty_sid = self->sid;
	guest_tty_fg_pgrp = self->pgid;
	return self->sid;
}

int bfree_setpgid(struct bfree_proc_mgr *mgr, int pid, int pgid)
{
	struct bfree_proc *p;

	if (pgid < 0)
		return -EINVAL;
	p = find_pid(mgr, pid);
	if (p == NULL)
		return -ESRCH;
	if (pgid == 0)
		pgid = p->pid;
	p->pgid = pgid;
	return 0;
}

int bfree_getpgid(struct bfree_proc_mgr *mgr, int pid)
{
	struct bfree_proc *p;

	if (pid == 0) {
		p = bfree_proc_current(mgr);
		if (p == NULL)
			return -ESRCH;
		return p->pgid;
	}
	p = find_pid(mgr, pid);
	if (p == NULL)
		return -ESRCH;
	return p->pgid;
}

int bfree_tcsetpgrp(int tty_fd, int pgrp)
{
	(void)tty_fd;
	if (pgrp < 0)
		return -EINVAL;
	guest_tty_fg_pgrp = pgrp;
	return 0;
}

int bfree_tcgetpgrp(int tty_fd)
{
	(void)tty_fd;
	return guest_tty_fg_pgrp;
}

int bfree_tty_session(void)
{
	return guest_tty_sid;
}
