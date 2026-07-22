/*
 * Freestanding kernel guest bring-up (M10).
 */
#include "guest_kernel.h"
#include "debugcon.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "syscall.h"

#include <fcntl.h>

void bfree_kernel_guest_init(void)
{
	struct bfree_fs *fs;
	int fd;

	guest_init();
	fs = guest_fs();
	if (fs == NULL)
		return;

	fd = bfree_open(fs, "/dev/console", O_RDWR, 0);
	if (fd < 0)
		return;
	if (fd != 1)
		guest_dup2(guest_io_ctx(), fd, 1);
	if (fd != 2)
		guest_dup2(guest_io_ctx(), fd, 2);
	if (fd > 2)
		bfree_close(fs, fd);
}
