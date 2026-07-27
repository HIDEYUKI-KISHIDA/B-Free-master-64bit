/*
 * Cooperative scheduler: vfork, execve, fork, waitid, pipes, signals (M2).
 */
#include "process.h"
#include "vmm.h"
#include "elf_load.h"
#include "elf_host_run.h"
#include "elf_trap_exec.h"
#include "fs_ofd.h"

#ifdef BFREE_KERNEL_GUEST
#include "elf_user_exec.h"
#include "ring3_sched.h"
#endif
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

struct bfree_fs *bfree_proc_exec_fs(void)
{
	return g_exec_fs;
}

void bfree_proc_init(struct bfree_proc_mgr *mgr)
{
	int i;

	memset(mgr, 0, sizeof(*mgr));
	mgr->next_pid = 2;
	mgr->current = 0;
	mgr->preempt_quantum = 4;
	mgr->procs[0].pid = 1;
	mgr->procs[0].ppid = 0;
	mgr->procs[0].pgid = 1;
	mgr->procs[0].sid = 0;
	mgr->procs[0].state = BFREE_PROC_RUNNING;
	bfree_as_init(&mgr->procs[0].as, 65536);
	bfree_fs_init_proc_fds(mgr->procs[0].fd_ofd, mgr->procs[0].fd_flags);
	for (i = 0; i < BFREE_MAX_FD; i++)
		mgr->fd_pipe_map[i] = BFREE_FD_UNMAPPED;
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_proc_init(&mgr->procs[0]);
#endif
}

void bfree_proc_bind_fs(struct bfree_proc_mgr *mgr, struct bfree_fs *fs)
{
	struct bfree_proc *self;

	if (mgr == NULL || fs == NULL)
		return;
	self = current_proc(mgr);
	if (self == NULL)
		return;
	bfree_fs_bind_fd_table(fs, self->fd_ofd, self->fd_flags);
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
	if (parent->as.va_abs) {
		if (bfree_as_fork_copy(&child->as, &parent->as) < 0)
			return -ENOMEM;
	} else {
		bfree_as_init(&child->as, parent->as.size);
		memcpy(child->as.mem, parent->as.mem, parent->as.size);
	}
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_proc_init(child);
#endif

	parent->state = BFREE_PROC_BLOCKED_VFORK;
	parent->vfork_done = 0;
	mgr->current = child_idx;
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_on_vfork(mgr);
#endif
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
	if (parent->as.va_abs) {
		if (bfree_as_fork_copy(&child->as, &parent->as) < 0)
			return -ENOMEM;
	} else {
		bfree_as_init(&child->as, parent->as.size);
		memcpy(child->as.mem, parent->as.mem, parent->as.size);
	}

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
	struct bfree_proc *self;
	int argc;
	int rc;

	(void)envp;
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;

	if (g_exec_fs != NULL) {
#ifdef BFREE_KERNEL_GUEST
		rc = bfree_user_execve_ring3(g_exec_fs, path,
					      (char *const *)argv,
					      (char *const *)envp);
		if (rc == 0) {
#ifdef BFREE_KERNEL_GUEST
			bfree_ring3_on_execve_parent(mgr);
#endif
			return 0;
		}
		if (rc < 0)
			return rc;
#else
		struct bfree_elf_image img;
		int status;

		rc = bfree_elf_load_path(g_exec_fs, path, &self->as, &img);
		if (rc == 0) {
			rc = bfree_elf_trap_exec(&self->as, img.entry,
						 img.load_size, &status);
			if (rc != 0)
				rc = bfree_elf_host_exec(&self->as, img.entry,
							 img.load_size,
							 &status);
			if (rc == 0)
				bfree_exit(mgr, status);
			return rc;
		}
		if (rc != -ENOENT && rc != -ENOEXEC)
			return rc;
#endif
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
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_after_exit(mgr);
#endif
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
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;

	for (;;) {
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
		if (options & WNOHANG)
			return 0;
#ifdef BFREE_KERNEL_GUEST
		bfree_ring3_block(mgr, BFREE_PROC_BLOCKED_WAIT);
#else
		return -ECHILD;
#endif
	}
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
	bfree_fs_init_proc_fds(child->fd_ofd, child->fd_flags);
	if (g_exec_fs != NULL)
		bfree_fs_fork_fds(g_exec_fs, child->fd_ofd, child->fd_flags,
				  parent->fd_ofd, parent->fd_flags);
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_proc_init(child);
#endif
	if (bfree_as_fork_copy(&child->as, &parent->as) < 0) {
		child->state = BFREE_PROC_FREE;
		return -ENOMEM;
	}
#ifdef BFREE_KERNEL_GUEST
	bfree_ring3_on_fork(mgr, child_idx, child->pid);
#endif
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
			if (g_exec_fs != NULL)
				bfree_proc_bind_fs(mgr, g_exec_fs);
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
			mgr->fd_pipe_map[pipefd[0]] = i;
			mgr->fd_pipe_map[pipefd[1]] = i;
			mgr->fd_pipe_end[pipefd[0]] = 0;
			mgr->fd_pipe_end[pipefd[1]] = 1;
			return 0;
		}
	}
	return -EMFILE;
}

static struct bfree_pipe *pipe_for_fd(struct bfree_proc_mgr *mgr, int fd,
				      int write_end)
{
	int i;
	int idx;

	if (fd >= 0 && fd < BFREE_MAX_FD &&
	    mgr->fd_pipe_map[fd] != BFREE_FD_UNMAPPED) {
		idx = mgr->fd_pipe_map[fd];
		if (idx >= 0 && idx < BFREE_MAX_PIPE &&
		    mgr->pipes[idx].in_use &&
		    mgr->fd_pipe_end[fd] == write_end)
			return &mgr->pipes[idx];
		return NULL;
	}

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

int bfree_pipe_dup2(struct bfree_proc_mgr *mgr, int oldfd, int newfd)
{
	struct bfree_pipe *p;
	int idx;
	int end;

	if (newfd < 0 || newfd >= BFREE_MAX_FD)
		return -EBADF;
	if (oldfd == newfd)
		return newfd;

	p = pipe_for_fd(mgr, oldfd, 0);
	end = 0;
	if (p == NULL) {
		p = pipe_for_fd(mgr, oldfd, 1);
		end = 1;
	}
	if (p == NULL)
		return -EBADF;

	for (idx = 0; idx < BFREE_MAX_PIPE; idx++) {
		if (&mgr->pipes[idx] == p)
			break;
	}
	if (idx >= BFREE_MAX_PIPE)
		return -EBADF;

	if (mgr->fd_pipe_map[newfd] != BFREE_FD_UNMAPPED) {
		int old_idx = mgr->fd_pipe_map[newfd];
		int old_end = mgr->fd_pipe_end[newfd];

		if (old_idx >= 0 && old_idx < BFREE_MAX_PIPE &&
		    mgr->pipes[old_idx].in_use) {
			if (old_end)
				mgr->pipes[old_idx].write_ref--;
			else
				mgr->pipes[old_idx].read_ref--;
		}
	}

	mgr->fd_pipe_map[newfd] = idx;
	mgr->fd_pipe_end[newfd] = end;
	if (end)
		p->write_ref++;
	else
		p->read_ref++;
	return newfd;
}

int bfree_pipe_is_fd(struct bfree_proc_mgr *mgr, int fd)
{
	if (pipe_for_fd(mgr, fd, 0) != NULL)
		return 1;
	if (pipe_for_fd(mgr, fd, 1) != NULL)
		return 1;
	return 0;
}

ssize_t bfree_pipe_read(struct bfree_proc_mgr *mgr, int fd, void *buf,
			size_t count)
{
	struct bfree_pipe *p;
	size_t done = 0;
	int pipe_idx = -1;
	int i;

	p = pipe_for_fd(mgr, fd, 0);
	if (p == NULL)
		return -EBADF;
	for (i = 0; i < BFREE_MAX_PIPE; i++) {
		if (&mgr->pipes[i] == p) {
			pipe_idx = i;
			break;
		}
	}

	for (;;) {
		done = 0;
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
		if (done > 0 || p->write_ref == 0)
			return (ssize_t)done;
#ifdef BFREE_KERNEL_GUEST
		bfree_ring3_block(mgr, BFREE_PROC_BLOCKED_IO);
#else
		break;
#endif
		(void)pipe_idx;
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
#ifdef BFREE_KERNEL_GUEST
	{
		int i;

		for (i = 0; i < BFREE_MAX_PIPE; i++) {
			if (&mgr->pipes[i] == p) {
				bfree_ring3_wake_pipe_readers(mgr, i);
				break;
			}
		}
	}
#endif
	return (ssize_t)done;
}

void bfree_pipe_close(struct bfree_proc_mgr *mgr, int fd)
{
	int i;
	int idx;
	int end;

	if (fd >= 0 && fd < BFREE_MAX_FD &&
	    mgr->fd_pipe_map[fd] != BFREE_FD_UNMAPPED) {
		idx = mgr->fd_pipe_map[fd];
		end = mgr->fd_pipe_end[fd];
		mgr->fd_pipe_map[fd] = BFREE_FD_UNMAPPED;
		if (idx >= 0 && idx < BFREE_MAX_PIPE && mgr->pipes[idx].in_use) {
			if (end)
				mgr->pipes[idx].write_ref--;
			else
				mgr->pipes[idx].read_ref--;
			if (mgr->pipes[idx].read_ref == 0 &&
			    mgr->pipes[idx].write_ref == 0)
				mgr->pipes[idx].in_use = 0;
#ifdef BFREE_KERNEL_GUEST
			bfree_ring3_wake_pipe_readers(mgr, idx);
#endif
		}
		return;
	}

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

static short pipe_poll_events(struct bfree_proc_mgr *mgr, int fd, short events)
{
	struct bfree_pipe *p;
	short revents = 0;

	p = pipe_for_fd(mgr, fd, 0);
	if (p != NULL) {
		if ((events & BFREE_POLLIN) &&
		    (p->count > 0 || p->write_ref == 0))
			revents |= BFREE_POLLIN;
		return revents;
	}

	p = pipe_for_fd(mgr, fd, 1);
	if (p != NULL) {
		if ((events & BFREE_POLLOUT) && p->read_ref > 0 &&
		    p->count < BFREE_MAX_PIPE_BUF)
			revents |= BFREE_POLLOUT;
		return revents;
	}

	return 0;
}

#define BFREE_POLL_SPIN_ITERS 65536U

#ifdef BFREE_KERNEL_GUEST
#define BFREE_POLL_TSC_PER_MS 2000000ULL

static uint64_t bfree_poll_read_tsc(void)
{
	uint32_t lo;
	uint32_t hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

static void bfree_poll_delay_ms(int ms)
{
	uint64_t start;
	uint64_t deadline;

	if (ms <= 0)
		return;
	start = bfree_poll_read_tsc();
	deadline = start + (uint64_t)ms * BFREE_POLL_TSC_PER_MS;
	while (bfree_poll_read_tsc() < deadline)
		__asm__ volatile("pause");
}
#endif

static int poll_scan(struct bfree_proc_mgr *mgr, struct bfree_fs *fs,
		     struct bfree_pollfd *fds, unsigned int nfds)
{
	unsigned int i;
	int ready = 0;

	for (i = 0; i < nfds; i++) {
		short revents;

		fds[i].revents = 0;
		if (fds[i].fd < 0) {
			fds[i].revents = BFREE_POLLNVAL;
			ready++;
			continue;
		}

		revents = pipe_poll_events(mgr, fds[i].fd, fds[i].events);
		if (revents == 0 && !bfree_pipe_is_fd(mgr, fds[i].fd)) {
			struct bfree_ofd *ofd = bfree_ofd_for_fd(fs, fds[i].fd);

			if (ofd == NULL) {
				fds[i].revents = BFREE_POLLNVAL;
				ready++;
				continue;
			}
			if (fds[i].events & BFREE_POLLIN)
				revents |= BFREE_POLLIN;
			if (fds[i].events & BFREE_POLLOUT)
				revents |= BFREE_POLLOUT;
		}

		fds[i].revents = revents;
		if (revents != 0)
			ready++;
	}

	return ready;
}

int bfree_poll(struct bfree_proc_mgr *mgr, struct bfree_fs *fs,
	       struct bfree_pollfd *fds, unsigned int nfds, int timeout)
{
	int ready;

	if (fds == NULL)
		return -EFAULT;
	if (nfds == 0)
		return -EINVAL;

	for (;;) {
		ready = poll_scan(mgr, fs, fds, nfds);
		if (ready > 0)
			return ready;
		if (timeout == 0)
			return 0;
#ifdef BFREE_KERNEL_GUEST
		if (timeout > 0) {
			bfree_poll_delay_ms(1);
			timeout--;
		} else {
			bfree_ring3_block(mgr, BFREE_PROC_BLOCKED_IO);
		}
#else
		{
			unsigned int spin;

			for (spin = 0; spin < BFREE_POLL_SPIN_ITERS; spin++)
				__asm__ volatile("pause");
		}
		if (timeout > 0)
			timeout--;
#endif
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
	else if (sig == BFREE_SIGCHLD)
		p->sigchld_pending = 1;
	else if (sig == BFREE_SIGPIPE)
		p->sigpipe_pending = 1;
	/* Handler installed: treat as delivered for pending query. */
	if (sig > 0 && sig < 64 && p->sig_handler[sig] != 0 &&
	    p->sig_handler[sig] != 1 /* SIG_IGN */) {
		if (sig == BFREE_SIGINT)
			p->sigint_pending = 1;
		else if (sig == BFREE_SIGPIPE)
			p->sigpipe_pending = 1;
		else if (sig == BFREE_SIGCHLD)
			p->sigchld_pending = pid > 0 ? pid : 1;
	}
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

int bfree_rt_sigaction(struct bfree_proc_mgr *mgr, int sig, const void *act,
		       void *oact, size_t sigsetsize)
{
	struct bfree_proc *self;
	unsigned long handler = 0;

	(void)sigsetsize;
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;
	if (sig <= 0 || sig >= 64)
		return -EINVAL;
	if (oact != NULL)
		*(unsigned long *)oact = self->sig_handler[sig];
	if (act != NULL) {
		handler = *(const unsigned long *)act;
		self->sig_handler[sig] = handler;
	}
	return 0;
}

int bfree_rt_sigprocmask(struct bfree_proc_mgr *mgr, int how, const void *set,
			 void *oset, size_t sigsetsize)
{
	struct bfree_proc *self;
	unsigned long newsig = 0;

	(void)sigsetsize;
	self = current_proc(mgr);
	if (self == NULL)
		return -ESRCH;
	if (oset != NULL)
		*(unsigned long *)oset = self->sig_mask;
	if (set == NULL)
		return 0;
	newsig = *(const unsigned long *)set;
	switch (how) {
	case 0: /* SIG_BLOCK */
		self->sig_mask |= newsig;
		break;
	case 1: /* SIG_UNBLOCK */
		self->sig_mask &= ~newsig;
		break;
	case 2: /* SIG_SETMASK */
		self->sig_mask = newsig;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

void bfree_sched_tick(struct bfree_proc_mgr *mgr)
{
	struct bfree_proc *self;
	int i;
	int next = -1;

	if (mgr == NULL)
		return;
	mgr->global_ticks++;
	self = current_proc(mgr);
	if (self != NULL)
		self->ticks++;
	if (self == NULL || mgr->preempt_quantum <= 0)
		return;
	if ((self->ticks % (unsigned long)mgr->preempt_quantum) != 0)
		return;
	for (i = 1; i <= BFREE_MAX_PROC; i++) {
		int idx = (mgr->current + i) % BFREE_MAX_PROC;
		if (mgr->procs[idx].state == BFREE_PROC_RUNNABLE) {
			next = idx;
			break;
		}
	}
	if (next < 0)
		return;
	if (self->state == BFREE_PROC_RUNNING)
		self->state = BFREE_PROC_RUNNABLE;
	mgr->current = next;
	mgr->procs[next].state = BFREE_PROC_RUNNING;
	if (g_exec_fs != NULL)
		bfree_proc_bind_fs(mgr, g_exec_fs);
}

int bfree_sched_yield(struct bfree_proc_mgr *mgr)
{
	if (mgr == NULL)
		return -EINVAL;
	bfree_sched_tick(mgr);
	return 0;
}

unsigned long bfree_sched_ticks(struct bfree_proc_mgr *mgr)
{
	return mgr != NULL ? mgr->global_ticks : 0;
}
