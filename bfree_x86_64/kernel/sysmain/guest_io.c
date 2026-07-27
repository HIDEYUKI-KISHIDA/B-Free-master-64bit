#include "guest_io.h"

#include <errno.h>

static int is_pipe_fd(struct bfree_proc_mgr *proc, int fd)
{
	return bfree_pipe_is_fd(proc, fd);
}

void guest_io_init(struct guest_io *io, struct bfree_fs *fs,
		   struct bfree_proc_mgr *proc)
{
	io->fs = fs;
	io->proc = proc;
}

ssize_t guest_read(struct guest_io *io, int fd, void *buf, size_t count)
{
	if (io == NULL || io->fs == NULL || io->proc == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd))
		return bfree_pipe_read(io->proc, fd, buf, count);
	return bfree_read(io->fs, fd, buf, count);
}

ssize_t guest_write(struct guest_io *io, int fd, const void *buf, size_t count)
{
	if (io == NULL || io->fs == NULL || io->proc == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd))
		return bfree_pipe_write(io->proc, fd, buf, count);
	return bfree_write(io->fs, fd, buf, count);
}

off_t guest_lseek(struct guest_io *io, int fd, off_t offset, int whence)
{
	if (io == NULL || io->fs == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd))
		return -ESPIPE;
	return bfree_lseek(io->fs, fd, offset, whence);
}

int guest_close(struct guest_io *io, int fd)
{
	if (io == NULL || io->fs == NULL || io->proc == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd)) {
		bfree_pipe_close(io->proc, fd);
		return 0;
	}
	return bfree_close(io->fs, fd);
}

int guest_dup(struct guest_io *io, int fd)
{
	if (io == NULL || io->fs == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd))
		return -EBADF;
	return bfree_dup(io->fs, fd);
}

int guest_dup2(struct guest_io *io, int oldfd, int newfd)
{
	if (io == NULL || io->fs == NULL || io->proc == NULL)
		return -EINVAL;
	if (newfd < 0 || newfd >= BFREE_MAX_FD)
		return -EBADF;
	if (oldfd == newfd)
		return newfd;
	if (is_pipe_fd(io->proc, oldfd))
		return bfree_pipe_dup2(io->proc, oldfd, newfd);
	return bfree_dup2(io->fs, oldfd, newfd);
}

int guest_fcntl(struct guest_io *io, int fd, int cmd, long arg)
{
	if (io == NULL || io->fs == NULL || io->proc == NULL)
		return -EINVAL;
	if (is_pipe_fd(io->proc, fd))
		return bfree_pipe_fcntl(io->proc, fd, cmd, arg);
	return bfree_fcntl(io->fs, fd, cmd, arg);
}
