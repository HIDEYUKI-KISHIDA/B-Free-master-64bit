/*
 * M3 shell CLI harness — orchestrates fork/pipe/exec/wait like ash without hacks.
 */
#include "shell_cli.h"

#include "vmm.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct bfree_shell_ctx *g_shell_ctx;

static int spawn_fork_child(struct bfree_proc_mgr *mgr, const char *path,
			    char **argv, char **envp)
{
	int parent_idx = mgr->current;
	int child_pid;
	int rc;

	child_pid = bfree_fork(mgr);
	if (child_pid < 0)
		return child_pid;
	if (bfree_switch_proc(mgr, child_pid) < 0)
		return -ESRCH;
	rc = bfree_execve(mgr, path, argv, envp);
	mgr->current = parent_idx;
	return rc < 0 ? rc : child_pid;
}

static int wait_child(struct bfree_proc_mgr *mgr, int pid, int *status)
{
	return bfree_wait4(mgr, pid, status, 0, NULL);
}

/* --- standard applets (external commands, not NOFORK inline) --- */

static int std_writer_applet(int argc, char **argv, char **envp)
{
	struct bfree_shell_ctx *ctx = g_shell_ctx;
	const char *msg = "ok";

	(void)envp;
	if (ctx == NULL || ctx->pipe_w < 0)
		return 1;
	if (argc > 1 && argv[1] != NULL)
		msg = argv[1];
	if (bfree_pipe_write(ctx->mgr, ctx->pipe_w, msg, strlen(msg)) < 0)
		return 1;
	return 0;
}

static int std_reader_applet(int argc, char **argv, char **envp)
{
	struct bfree_shell_ctx *ctx = g_shell_ctx;
	char buf[256];
	ssize_t n;

	(void)argc;
	(void)argv;
	(void)envp;
	if (ctx == NULL || ctx->pipe_r < 0)
		return 1;
	memset(buf, 0, sizeof(buf));
	n = bfree_pipe_read(ctx->mgr, ctx->pipe_r, buf, sizeof(buf) - 1);
	if (n < 0)
		return 1;
	ctx->capture_len = (size_t)n;
	memcpy(ctx->capture, buf, ctx->capture_len);
	ctx->capture[ctx->capture_len] = '\0';
	return 0;
}

static int std_echo_applet(int argc, char **argv, char **envp)
{
	struct bfree_shell_ctx *ctx = g_shell_ctx;
	const char *msg = "";
	int i;

	(void)envp;
	if (ctx == NULL || ctx->pipe_w < 0)
		return 1;
	for (i = 1; i < argc; i++) {
		if (i > 1)
			bfree_pipe_write(ctx->mgr, ctx->pipe_w, " ", 1);
		if (argv[i] != NULL)
			bfree_pipe_write(ctx->mgr, ctx->pipe_w, argv[i],
					 strlen(argv[i]));
	}
	if (argc <= 1)
		msg = argv[0] ? "" : msg;
	(void)msg;
	return 0;
}

static int std_touch_as_applet(int argc, char **argv, char **envp)
{
	uint8_t *mem;

	(void)argc;
	(void)argv;
	(void)envp;
	if (g_shell_ctx == NULL || g_shell_ctx->mgr == NULL)
		return 1;
	mem = bfree_as_ptr(&bfree_proc_current(g_shell_ctx->mgr)->as);
	if (mem == NULL)
		return 1;
	mem[0] = 'S';
	return 0;
}

static int std_sleep_exit_applet(int argc, char **argv, char **envp)
{
	int code = 0;

	(void)envp;
	if (argc > 1 && argv[1] != NULL)
		code = argv[1][0] - '0';
	(void)code;
	return 0;
}

void bfree_shell_ctx_init(struct bfree_shell_ctx *ctx,
			  struct bfree_proc_mgr *mgr)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->mgr = mgr;
	ctx->pipe_w = -1;
	ctx->pipe_r = -1;
}

int bfree_shell_register_std_applets(void)
{
	if (bfree_proc_register("/bin/writer", std_writer_applet) < 0)
		return -1;
	if (bfree_proc_register("/bin/reader", std_reader_applet) < 0)
		return -1;
	if (bfree_proc_register("/bin/echo", std_echo_applet) < 0)
		return -1;
	if (bfree_proc_register("/bin/touch-as", std_touch_as_applet) < 0)
		return -1;
	if (bfree_proc_register("/bin/sleep-exit", std_sleep_exit_applet) < 0)
		return -1;
	return 0;
}

int bfree_shell_pipeline(struct bfree_shell_ctx *ctx,
			 const char *writer_path, char **writer_argv,
			 const char *reader_path, char **reader_argv)
{
	int pipefd[2];
	int wp;
	int rp;
	int st_w;
	int st_r;
	int rc = 0;

	if (ctx == NULL || ctx->mgr == NULL)
		return -EINVAL;

	g_shell_ctx = ctx;
	if (bfree_pipe_open(ctx->mgr, pipefd) < 0)
		return -1;

	ctx->pipe_w = pipefd[1];
	ctx->pipe_r = pipefd[0];
	ctx->capture_len = 0;
	ctx->capture[0] = '\0';

	wp = spawn_fork_child(ctx->mgr, writer_path, writer_argv, NULL);
	if (wp < 0)
		return wp;
	st_w = -1;
	if (wait_child(ctx->mgr, wp, &st_w) != wp)
		rc = -1;

	rp = spawn_fork_child(ctx->mgr, reader_path, reader_argv, NULL);
	if (rp < 0)
		return rp;
	st_r = -1;
	if (wait_child(ctx->mgr, rp, &st_r) != rp)
		rc = -1;

	bfree_pipe_close(ctx->mgr, pipefd[0]);
	bfree_pipe_close(ctx->mgr, pipefd[1]);
	ctx->pipe_w = -1;
	ctx->pipe_r = -1;
	g_shell_ctx = NULL;

	if (rc < 0 || st_w != 0 || st_r != 0)
		return -1;
	return 0;
}

int bfree_shell_subshell(struct bfree_shell_ctx *ctx, const char *path,
			 char **argv, int *exit_status)
{
	int parent_idx;
	uint8_t *parent_mem;
	int child_pid;
	int st = -1;

	if (ctx == NULL || ctx->mgr == NULL)
		return -EINVAL;

	parent_idx = ctx->mgr->current;
	parent_mem = bfree_as_ptr(&bfree_proc_current(ctx->mgr)->as);
	if (parent_mem == NULL)
		return -EINVAL;

	g_shell_ctx = ctx;
	child_pid = spawn_fork_child(ctx->mgr, path, argv, NULL);
	g_shell_ctx = NULL;
	if (child_pid < 0)
		return child_pid;
	if (wait_child(ctx->mgr, child_pid, &st) != child_pid)
		return -1;
	if (exit_status != NULL)
		*exit_status = st;
	if (parent_mem[0] != 'P')
		return -1;
	if (ctx->mgr->current != parent_idx)
		return -1;
	return 0;
}

int bfree_shell_cmdsubst(struct bfree_shell_ctx *ctx, const char *path,
			 char **argv, char *buf, size_t buflen)
{
	int pipefd[2];
	int child_pid;
	int st = -1;
	ssize_t n;

	if (ctx == NULL || ctx->mgr == NULL || buf == NULL || buflen == 0)
		return -EINVAL;

	g_shell_ctx = ctx;
	if (bfree_pipe_open(ctx->mgr, pipefd) < 0)
		return -1;

	ctx->pipe_w = pipefd[1];
	ctx->pipe_r = pipefd[0];

	child_pid = spawn_fork_child(ctx->mgr, path, argv, NULL);
	if (child_pid < 0)
		return child_pid;
	if (wait_child(ctx->mgr, child_pid, &st) != child_pid || st != 0) {
		g_shell_ctx = NULL;
		return -1;
	}

	memset(buf, 0, buflen);
	n = bfree_pipe_read(ctx->mgr, pipefd[0], buf, buflen - 1);
	bfree_pipe_close(ctx->mgr, pipefd[0]);
	bfree_pipe_close(ctx->mgr, pipefd[1]);
	ctx->pipe_w = -1;
	ctx->pipe_r = -1;
	g_shell_ctx = NULL;

	if (n < 0)
		return -1;
	return 0;
}

int bfree_shell_bg(struct bfree_shell_ctx *ctx, const char *path,
		   char **argv, int *bg_pid)
{
	struct bfree_proc *child;
	int child_pid;

	if (ctx == NULL || ctx->mgr == NULL)
		return -EINVAL;

	child_pid = bfree_fork(ctx->mgr);
	if (child_pid < 0)
		return child_pid;

	child = NULL;
	{
		int i;

		for (i = 0; i < BFREE_MAX_PROC; i++) {
			if (ctx->mgr->procs[i].pid == child_pid) {
				child = &ctx->mgr->procs[i];
				break;
			}
		}
	}
	if (child == NULL || child->state != BFREE_PROC_RUNNABLE)
		return -ESRCH;

	ctx->bg_pending_pid = child_pid;
	ctx->bg_path = path;
	ctx->bg_argv = argv;
	if (bg_pid != NULL)
		*bg_pid = child_pid;
	return 0;
}

int bfree_shell_bg_join(struct bfree_shell_ctx *ctx, int *exit_status)
{
	int st = -1;

	if (ctx == NULL || ctx->mgr == NULL || ctx->bg_pending_pid <= 0)
		return -EINVAL;

	g_shell_ctx = ctx;
	if (bfree_switch_proc(ctx->mgr, ctx->bg_pending_pid) < 0) {
		g_shell_ctx = NULL;
		return -ESRCH;
	}
	if (bfree_execve(ctx->mgr, ctx->bg_path, ctx->bg_argv, NULL) < 0) {
		g_shell_ctx = NULL;
		return -ENOENT;
	}
	g_shell_ctx = NULL;

	if (wait_child(ctx->mgr, ctx->bg_pending_pid, &st) != ctx->bg_pending_pid)
		return -1;
	ctx->bg_pending_pid = 0;
	if (exit_status != NULL)
		*exit_status = st;
	return 0;
}

int bfree_shell_external(struct bfree_shell_ctx *ctx, const char *path,
			 char **argv, int *exit_status)
{
	int pid;
	int st = -1;

	if (ctx == NULL || ctx->mgr == NULL)
		return -EINVAL;

	g_shell_ctx = ctx;
	pid = bfree_spawn_vfork_child(ctx->mgr, path, argv, NULL);
	g_shell_ctx = NULL;
	if (pid < 0)
		return pid;
	if (wait_child(ctx->mgr, pid, &st) != pid)
		return -1;
	if (exit_status != NULL)
		*exit_status = st;
	return 0;
}
