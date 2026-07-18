/*
 * Cooperative scheduler: vfork, execve, fork, waitid, pipes, signals (M2).
 */
#include "process.h"
#include "vmm.h"
#include "elf_load.h"
#include "elf_host_run.h"
#include "fs_ofd.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct bfree_prog_entry {
	char          path[64];
	bfree_prog_fn fn;
};

static struct bfree_prog_entry prog_table[BFREE_MAX_PROG];
static int prog_count;
static struct bfree_fs *g_exec_fs;

static struct bfree_proc *proc_at(struct bfree_proc_mgr *mgr, int idx)
{
	if (idx < 0 || idx >= BFREE_MAX_PROC)
		return NULL;
	if (mgr->procs[idx].state == BFREE_PROC_FREE)
		return NULL;
	return &mgr->procs[idx];
}

static int alloc_slot(struct bfree_proc_mgr *mgr)
{
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state == BFREE_PROC_FREE)
			return i;
	}
	return -1;
}

static struct bfree_proc *current_proc(struct bfree_proc_mgr *mgr)
{
	return proc_at(mgr, mgr->current);
}

static struct bfree_proc *find_pid(struct bfree_proc_mgr *mgr, int pid)
{
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state != BFREE_PROC_FREE &&
		    mgr->procs[i].pid == pid)
			return &mgr->procs[i];
	}
	return NULL;
}

static bfree_prog_fn lookup_prog(const char *path)
{
	int i;

	for (i = 0; i < prog_count; i++) {
		if (strcmp(prog_table[i].path, path) == 0)
			return prog_table[i].fn;
	}
	return NULL;
}

static void wake_parent_vfork(struct bfree_proc_mgr *mgr,
			      struct bfree_proc *child)
{
	struct bfree_proc *parent = NULL;
	int parent_idx = -1;
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state != BFREE_PROC_FREE &&
		    mgr->procs[i].pid == child->ppid) {
			parent = &mgr->procs[i];
			parent_idx = i;
			break;
		}
	}
	if (parent == NULL || parent_idx < 0)
		return;
	if (parent->state == BFREE_PROC_BLOCKED_VFORK) {
		parent->state = BFREE_PROC_RUNNING;
		parent->vfork_done = child->pid;
		mgr->current = parent_idx;
	}
}

static void notify_sigchld(struct bfree_proc_mgr *mgr, struct bfree_proc *child)
{
	struct bfree_proc *parent;

	parent = find_pid(mgr, child->ppid);
	if (parent != NULL)
		parent->sigchld_pending = child->pid;
}

void bfree_proc_attach_fs(struct bfree_fs *fs)
{
	g_exec_fs = fs;
}

void bfree_proc_init(struct bfree_proc_mgr *mgr)
{
	memset(mgr, 0, sizeof(*mgr));
	mgr->next_pid = 2;
	mgr->current = 0;
	mgr->procs[0].pid = 1;
	mgr->procs[0].ppid = 0;
	mgr->procs[0].pgid = 1;
	mgr->procs[0].sid = 0;
	mgr->procs[0].state = BFREE_PROC_RUNNING;
	bfree_as_init(&mgr->procs[0].as, 65536);
}

int bfree_proc_register(const char *path, bfree_prog_fn fn)
{
	if (path == NULL || fn == NULL || prog_count >= BFREE_MAX_PROG)
		return -ENOMEM;
	snprintf(prog_table[prog_count].path,
		 sizeof(prog_table[prog_count].path), "%s", path);
	prog_table[prog_count].fn = fn;
	prog_count++;
	return 0;
}

int bfree_vfork(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *parent;
	struct bfree_proc *child;
	int child_idx;

	parent = current_proc(mgr);
	if (parent == NULL)
		return -ESRCH;
	child_idx = alloc_slot(mgr);
	if (child_idx < 0)
		return -EAGAIN;

	child = &mgr->procs[child_idx];
	memset(child, 0, sizeof(*child));
	child->pid = mgr->next_pid++;
	child->ppid = parent->pid;
	child->state = BFREE_PROC_RUNNING;
	bfree_as_init(&child->as, parent->as.size);
	memcpy(child->as.mem, parent->as.mem, parent->as.size);

	parent->state = BFREE_PROC_BLOCKED_VFORK;
	parent->vfork_done = 0;
	mgr->current = child_idx;
	return 0;
}

int bfree_spawn_vfork_child(struct bfree_proc_mgr *mgr, const char *path,
			    char **argv, char **envp)
{
	int parent_idx = mgr->current;
	struct bfree_proc *parent = &mgr->procs[parent_idx];
	struct bfree_proc *child;
	int child_idx;
	int child_pid;
	int rc;

	child_idx = alloc_slot(mgr);
	if (child_idx < 0)
		return -EAGAIN;

	child = &mgr->procs[child_idx];
	memset(child, 0, sizeof(*child));
	child->pid = mgr->next_pid++;
	child_pid = child->pid;
	child->ppid = parent->pid;
	child->state = BFREE_PROC_RUNNING;
	bfree_as_init(&child->as, parent->as.size);
	memcpy(child->as.mem, parent->as.mem, parent->as.size);

	parent->state = BFREE_PROC_BLOCKED_VFORK;
	parent->vfork_done = 0;
	mgr->current = child_idx;

	rc = bfree_execve(mgr, path, argv, envp);
	parent->state = BFREE_PROC_RUNNING;
	mgr->current = parent_idx;
	if (rc < 0)
		return rc;
	return child_pid;
}

int bfree_execve(struct bfree_proc_mgr *mgr, const char *path,
		 char **argv, char **envp)
{
	bfree_prog_fn fn;
	struct bfree_elf_image img;
	struct bfree_proc *self;
	int argc;
	int rc;
	int status;

	(void)envp;
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;

	if (g_exec_fs != NULL) {
		rc = bfree_elf_load_path(g_exec_fs, path, &self->as, &img);
		if (rc == 0) {
			rc = bfree_elf_host_exec(&self->as, img.entry,
						 img.load_size, &status);
			if (rc == 0)
				bfree_exit(mgr, status);
			return rc;
		}
		if (rc != -ENOENT && rc != -ENOEXEC)
			return rc;
	}

	fn = lookup_prog(path);
	if (fn == NULL)
		return -ENOENT;
	for (argc = 0; argv && argv[argc]; argc++)
		;
	rc = fn(argc, argv, envp);
	bfree_exit(mgr, rc);
	return 0;
}

void bfree_exit(struct bfree_proc_mgr *mgr, int status)
{
	struct bfree_proc *self;
	int parent_idx = -1;
	int j;

	self = current_proc(mgr);
	if (self == NULL)
		return;
	for (j = 0; j < BFREE_MAX_PROC; j++) {
		if (mgr->procs[j].state != BFREE_PROC_FREE &&
		    mgr->procs[j].pid == self->ppid) {
			parent_idx = j;
			break;
		}
	}

	self->exit_status = status & 0xff;
	self->state = BFREE_PROC_ZOMBIE;
	notify_sigchld(mgr, self);

	if (parent_idx >= 0 &&
	    mgr->procs[parent_idx].state == BFREE_PROC_BLOCKED_VFORK) {
		wake_parent_vfork(mgr, self);
		return;
	}
	if (parent_idx >= 0)
		mgr->current = parent_idx;
}

static int reap_one(struct bfree_proc_mgr *mgr, struct bfree_proc *zombie,
		    int *status)
{
	int pid = zombie->pid;

	if (status != NULL)
		*status = zombie->exit_status;
	if (!zombie->as_shared)
		bfree_as_free(&zombie->as);
	memset(zombie, 0, sizeof(*zombie));
	zombie->state = BFREE_PROC_FREE;
	(void)mgr;
	return pid;
}

int bfree_wait4(struct bfree_proc_mgr *mgr, int pid, int *status,
		int options, void *rusage)
{
	struct bfree_proc *self;
	int i;

	(void)rusage;
	(void)options;
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		struct bfree_proc *p = &mgr->procs[i];

		if (p->state != BFREE_PROC_ZOMBIE)
			continue;
		if (p->ppid != self->pid)
			continue;
		if (pid > 0 && p->pid != pid)
			continue;
		if (pid == 0 || pid == -1 || p->pid == pid)
			return reap_one(mgr, p, status);
	}
	return -ECHILD;
}

int bfree_waitid(struct bfree_proc_mgr *mgr, int idtype, int id,
		 int *status, int options)
{
	struct bfree_proc *self;
	int i;
	int found = 0;

	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		struct bfree_proc *p = &mgr->procs[i];

		if (p->state != BFREE_PROC_ZOMBIE || p->ppid != self->pid)
			continue;
		if (idtype == P_PID && p->pid != id)
			continue;
		found = 1;
		if (options & WNOHANG) {
			reap_one(mgr, p, status);
			return 0;
		}
		return reap_one(mgr, p, status);
	}
	if (options & WNOHANG)
		return 0;
	if (!found && idtype == P_PID)
		return -ECHILD;
	return -ECHILD;
}

int bfree_fork(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *parent;
	struct bfree_proc *child;
	int child_idx;

	parent = current_proc(mgr);
	if (parent == NULL)
		return -ESRCH;
	child_idx = alloc_slot(mgr);
	if (child_idx < 0)
		return -EAGAIN;

	child = &mgr->procs[child_idx];
	memset(child, 0, sizeof(*child));
	child->pid = mgr->next_pid++;
	child->ppid = parent->pid;
	child->pgid = parent->pgid;
	child->sid = parent->sid;
	child->state = BFREE_PROC_RUNNABLE;
	if (bfree_as_fork_copy(&child->as, &parent->as) < 0) {
		child->state = BFREE_PROC_FREE;
		return -ENOMEM;
	}
	return child->pid;
}

int bfree_switch_proc(struct bfree_proc_mgr *mgr, int pid)
{
	struct bfree_proc *p;
	int i;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state != BFREE_PROC_FREE &&
		    mgr->procs[i].pid == pid) {
			mgr->current = i;
			return 0;
		}
	}
	p = find_pid(mgr, pid);
	if (p == NULL)
		return -ESRCH;
	return 0;
}

int bfree_pipe_open(struct bfree_proc_mgr *mgr, int pipefd[2])
{
	int i;
	static int next_fd = 100;

	for (i = 0; i < BFREE_MAX_PIPE; i++) {
		if (!mgr->pipes[i].in_use) {
			memset(&mgr->pipes[i], 0, sizeof(mgr->pipes[i]));
			mgr->pipes[i].in_use = 1;
			mgr->pipes[i].read_ref = 1;
			mgr->pipes[i].write_ref = 1;
			mgr->pipes[i].read_fd = next_fd++;
			mgr->pipes[i].write_fd = next_fd++;
			pipefd[0] = mgr->pipes[i].read_fd;
			pipefd[1] = mgr->pipes[i].write_fd;
			return 0;
		}
	}
	return -EMFILE;
}

static struct bfree_pipe *pipe_for_fd(struct bfree_proc_mgr *mgr, int fd,
				      int write_end)
{
	int i;

	for (i = 0; i < BFREE_MAX_PIPE; i++) {
		if (!mgr->pipes[i].in_use)
			continue;
		if (!write_end && fd == mgr->pipes[i].read_fd)
			return &mgr->pipes[i];
		if (write_end && fd == mgr->pipes[i].write_fd)
			return &mgr->pipes[i];
	}
	return NULL;
}

ssize_t bfree_pipe_read(struct bfree_proc_mgr *mgr, int fd, void *buf,
			size_t count)
{
	struct bfree_pipe *p;
	size_t done = 0;

	p = pipe_for_fd(mgr, fd, 0);
	if (p == NULL)
		return -EBADF;
	while (done < count) {
		if (p->count == 0) {
			if (p->write_ref == 0)
				return (ssize_t)done;
			break;
		}
		((char *)buf)[done++] = p->buf[p->head];
		p->head = (p->head + 1) % BFREE_MAX_PIPE_BUF;
		p->count--;
	}
	return (ssize_t)done;
}

ssize_t bfree_pipe_write(struct bfree_proc_mgr *mgr, int fd, const void *buf,
			 size_t count)
{
	struct bfree_pipe *p;
	struct bfree_proc *self;
	size_t done = 0;

	p = pipe_for_fd(mgr, fd, 1);
	if (p == NULL)
		return -EBADF;
	if (p->read_ref == 0) {
		self = current_proc(mgr);
		if (self != NULL)
			self->sigpipe_pending = 1;
		return -EPIPE;
	}
	while (done < count) {
		if (p->count >= BFREE_MAX_PIPE_BUF)
			break;
		p->buf[p->tail] = ((const char *)buf)[done++];
		p->tail = (p->tail + 1) % BFREE_MAX_PIPE_BUF;
		p->count++;
	}
	return (ssize_t)done;
}

void bfree_pipe_close(struct bfree_proc_mgr *mgr, int fd)
{
	int i;

	for (i = 0; i < BFREE_MAX_PIPE; i++) {
		if (!mgr->pipes[i].in_use)
			continue;
		if (fd == mgr->pipes[i].read_fd) {
			mgr->pipes[i].read_ref--;
			if (mgr->pipes[i].read_ref == 0 &&
			    mgr->pipes[i].write_ref == 0)
				mgr->pipes[i].in_use = 0;
			return;
		}
		if (fd == mgr->pipes[i].write_fd) {
			mgr->pipes[i].write_ref--;
			if (mgr->pipes[i].read_ref == 0 &&
			    mgr->pipes[i].write_ref == 0)
				mgr->pipes[i].in_use = 0;
			return;
		}
	}
}

void bfree_kill(struct bfree_proc_mgr *mgr, int pid, int sig)
{
	struct bfree_proc *p;

	p = find_pid(mgr, pid);
	if (p == NULL)
		return;
	if (sig == BFREE_SIGINT)
		p->sigint_pending = 1;
}

int bfree_sig_pending(struct bfree_proc_mgr *mgr, int sig)
{
	struct bfree_proc *self;

	self = current_proc(mgr);
	if (self == NULL)
		return 0;
	if (sig == BFREE_SIGCHLD && self->sigchld_pending) {
		self->sigchld_pending = 0;
		return 1;
	}
	if (sig == BFREE_SIGINT && self->sigint_pending) {
		self->sigint_pending = 0;
		return 1;
	}
	if (sig == BFREE_SIGPIPE && self->sigpipe_pending) {
		self->sigpipe_pending = 0;
		return 1;
	}
	return 0;
}

struct bfree_proc *bfree_proc_current(struct bfree_proc_mgr *mgr)
{
	return current_proc(mgr);
}

int bfree_proc_zombie_count(struct bfree_proc_mgr *mgr)
{
	int i;
	int n = 0;

	for (i = 0; i < BFREE_MAX_PROC; i++) {
		if (mgr->procs[i].state == BFREE_PROC_ZOMBIE)
			n++;
	}
	return n;
}
