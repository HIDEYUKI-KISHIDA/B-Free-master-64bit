/*
 * Freestanding kernel guest bring-up (M10).
 */
#include "guest_initramfs_seed.h"
#include "guest_kernel.h"
#include "debugcon.h"
#include "fs_ofd.h"
#include "guest_io.h"
#include "process.h"
#include "procfs.h"
#include "syscall.h"
#include "vmm.h"

#include <fcntl.h>

void bfree_kernel_guest_init(void)
{
	struct bfree_fs *fs;
	struct bfree_proc_mgr *mgr;
	int fd;

	guest_init();
	fs = guest_fs();
	if (fs == NULL)
		return;

	/* Switch pid1 address space to identity-mapped user VAs (L2). */
	mgr = guest_proc_mgr();
	if (mgr != NULL)
		(void)bfree_as_init_user_va(&mgr->procs[0].as,
					    BFREE_USER_HEAP_BASE,
					    BFREE_USER_MMAP_TOP,
					    BFREE_USER_HEAP_BASE);

	fd = bfree_open(fs, "/dev/console", O_RDWR, 0);
	if (fd < 0)
		return;
	if (fd != 1)
		guest_dup2(guest_io_ctx(), fd, 1);
	if (fd != 2)
		guest_dup2(guest_io_ctx(), fd, 2);
	if (fd > 2)
		bfree_close(fs, fd);

	bfree_guest_initramfs_seed_vfs(fs);
	(void)bfree_procfs_init(fs);
}
