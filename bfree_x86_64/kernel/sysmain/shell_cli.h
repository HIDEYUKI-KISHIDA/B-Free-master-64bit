/*
 * M3 normal CLI harness — real fork/pipe/wait (no inproc pipe, bg-inline, NOFORK-all).
 */
#ifndef BFREE_SHELL_CLI_H
#define BFREE_SHELL_CLI_H

#include "process.h"

#include <stddef.h>

struct bfree_shell_ctx {
	struct bfree_proc_mgr *mgr;
	int                    pipe_w;
	int                    pipe_r;
	char                   capture[256];
	size_t                 capture_len;
	int                    bg_pending_pid;
	const char            *bg_path;
	char                 **bg_argv;
};

void bfree_shell_ctx_init(struct bfree_shell_ctx *ctx,
			  struct bfree_proc_mgr *mgr);

/* Register built-in test applets used by shell regression tests. */
int bfree_shell_register_std_applets(void);

/*
 * Pipeline: fork writer + reader children connected by kernel pipe.
 * Replaces inproc pipe memcpy hack.
 */
int bfree_shell_pipeline(struct bfree_shell_ctx *ctx,
			 const char *writer_path, char **writer_argv,
			 const char *reader_path, char **reader_argv);

/*
 * Subshell: ( cmd ) — fork with eager-copy AS, parent memory unchanged.
 */
int bfree_shell_subshell(struct bfree_shell_ctx *ctx,
			 const char *path, char **argv, int *exit_status);

/*
 * Command substitution: $(cmd) — child writes to pipe, parent reads capture.
 */
int bfree_shell_cmdsubst(struct bfree_shell_ctx *ctx,
			 const char *path, char **argv,
			 char *buf, size_t buflen);

/*
 * Background job: cmd & — fork without blocking parent inline.
 * Replaces bg-inline synchronous hack.
 */
int bfree_shell_bg(struct bfree_shell_ctx *ctx,
		   const char *path, char **argv, int *bg_pid);

/* Run and reap a job started with bfree_shell_bg (parent continues first). */
int bfree_shell_bg_join(struct bfree_shell_ctx *ctx, int *exit_status);

/*
 * External command via execve (vfork child), not NOFORK inline execution.
 */
int bfree_shell_external(struct bfree_shell_ctx *ctx,
			 const char *path, char **argv, int *exit_status);

#endif /* BFREE_SHELL_CLI_H */
