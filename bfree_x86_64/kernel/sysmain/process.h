/*
 * Cooperative process model for bfree_x86_64 guest (M2).
 */
#ifndef BFREE_PROCESS_H
#define BFREE_PROCESS_H

#include "vmm.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct bfree_fs;

#define BFREE_MAX_PROC       16
#define BFREE_MAX_PROG        8
#define BFREE_MAX_PIPE        8
#define BFREE_MAX_PIPE_BUF 4096

#define BFREE_SIGCHLD  17
#define BFREE_SIGINT    2
#define BFREE_SIGPIPE  13

#define P_ALL    0
#define P_PID    1
#define P_PGID   2

#define WNOHANG  1
#define WEXITED  4

typedef int (*bfree_prog_fn)(int argc, char **argv, char **envp);
typedef int (*bfree_thread_fn)(void *arg);

typedef enum {
	BFREE_PROC_FREE = 0,
	BFREE_PROC_RUNNABLE,
	BFREE_PROC_RUNNING,
	BFREE_PROC_ZOMBIE,
	BFREE_PROC_BLOCKED_VFORK,
} bfree_proc_state_t;

struct bfree_proc {
	int               pid;
	int               ppid;
	int               pgid;
	int               sid;
	int               is_thread;
	int               as_shared;
	bfree_proc_state_t state;
	int               exit_status;
	int               vfork_done;
	struct bfree_as   as;
	bfree_thread_fn   thread_fn;
	void             *thread_arg;
	int               sigchld_pending;
	int               sigint_pending;
	int               sigpipe_pending;
};

struct bfree_pipe {
	int   in_use;
	int   read_fd;
	int   write_fd;
	int   read_ref;
	int   write_ref;
	char  buf[BFREE_MAX_PIPE_BUF];
	size_t head;
	size_t tail;
	size_t count;
};

struct bfree_proc_mgr {
	struct bfree_proc procs[BFREE_MAX_PROC];
	struct bfree_pipe pipes[BFREE_MAX_PIPE];
	int               current;
	int               next_pid;
};

void bfree_proc_init(struct bfree_proc_mgr *mgr);
void bfree_proc_attach_fs(struct bfree_fs *fs);

int  bfree_proc_register(const char *path, bfree_prog_fn fn);

int  bfree_vfork(struct bfree_proc_mgr *mgr);
int  bfree_spawn_vfork_child(struct bfree_proc_mgr *mgr, const char *path,
			     char **argv, char **envp);
int  bfree_execve(struct bfree_proc_mgr *mgr, const char *path,
		  char **argv, char **envp);
void bfree_exit(struct bfree_proc_mgr *mgr, int status);
int  bfree_wait4(struct bfree_proc_mgr *mgr, int pid, int *status,
		 int options, void *rusage);
int  bfree_waitid(struct bfree_proc_mgr *mgr, int idtype, int id,
		  int *status, int options);
int  bfree_fork(struct bfree_proc_mgr *mgr);
int  bfree_switch_proc(struct bfree_proc_mgr *mgr, int pid);

int  bfree_pipe_open(struct bfree_proc_mgr *mgr, int pipefd[2]);
ssize_t bfree_pipe_read(struct bfree_proc_mgr *mgr, int fd, void *buf,
			size_t count);
ssize_t bfree_pipe_write(struct bfree_proc_mgr *mgr, int fd,
			 const void *buf, size_t count);
void bfree_pipe_close(struct bfree_proc_mgr *mgr, int fd);

int bfree_pipe_is_fd(struct bfree_proc_mgr *mgr, int fd);

#define BFREE_POLLIN   0x0001
#define BFREE_POLLOUT  0x0004
#define BFREE_POLLNVAL 0x0020

struct bfree_pollfd {
	int   fd;
	short events;
	short revents;
};

int bfree_poll(struct bfree_proc_mgr *mgr, struct bfree_fs *fs,
	       struct bfree_pollfd *fds, unsigned int nfds, int timeout);

void bfree_kill(struct bfree_proc_mgr *mgr, int pid, int sig);
int  bfree_sig_pending(struct bfree_proc_mgr *mgr, int sig);

struct bfree_proc *bfree_proc_current(struct bfree_proc_mgr *mgr);
int bfree_proc_zombie_count(struct bfree_proc_mgr *mgr);

#endif /* BFREE_PROCESS_H */
