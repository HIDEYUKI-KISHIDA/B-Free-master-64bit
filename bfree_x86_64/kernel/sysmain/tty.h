/*
 * TTY session and process group helpers (M4 job control base).
 */
#ifndef BFREE_TTY_H
#define BFREE_TTY_H

#include "process.h"

int  bfree_setsid(struct bfree_proc_mgr *mgr);
int  bfree_setpgid(struct bfree_proc_mgr *mgr, int pid, int pgid);
int  bfree_getpgid(struct bfree_proc_mgr *mgr, int pid);
int  bfree_tcsetpgrp(int tty_fd, int pgrp);
int  bfree_tcgetpgrp(int tty_fd);

#endif /* BFREE_TTY_H */
