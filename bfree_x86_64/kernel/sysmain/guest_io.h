/*
 * Unified guest FD I/O — dispatches FS files and pipe ends (M4).
 */
#ifndef BFREE_GUEST_IO_H
#define BFREE_GUEST_IO_H

#include "fs_ofd.h"
#include "process.h"

#include <stddef.h>
#include <sys/types.h>

#define BFREE_FD_CLOEXEC 1

struct guest_io {
	struct bfree_fs       *fs;
	struct bfree_proc_mgr *proc;
};

void guest_io_init(struct guest_io *io, struct bfree_fs *fs,
		   struct bfree_proc_mgr *proc);

ssize_t guest_read(struct guest_io *io, int fd, void *buf, size_t count);
ssize_t guest_write(struct guest_io *io, int fd, const void *buf, size_t count);
off_t   guest_lseek(struct guest_io *io, int fd, off_t offset, int whence);
int     guest_close(struct guest_io *io, int fd);
int     guest_dup(struct guest_io *io, int fd);
int     guest_dup2(struct guest_io *io, int oldfd, int newfd);
int     guest_fcntl(struct guest_io *io, int fd, int cmd, long arg);

#endif /* BFREE_GUEST_IO_H */
