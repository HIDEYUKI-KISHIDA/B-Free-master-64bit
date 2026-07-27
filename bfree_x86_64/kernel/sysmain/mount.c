#include "mount.h"
#include "fs_ofd.h"

#include <errno.h>
#include <string.h>

int bfree_mount(struct bfree_fs *fs, const char *target, const char *source)
{
	if (fs == NULL || target == NULL || source == NULL)
		return -EINVAL;
	if (target[0] != '/')
		return -EINVAL;
	return bfree_fs_mount(fs, source);
}

int bfree_umount2(struct bfree_fs *fs, const char *target, int flags)
{
	(void)flags;
	if (fs == NULL || target == NULL || target[0] != '/')
		return -EINVAL;
	if (!fs->mounted)
		return -EINVAL;
	bfree_fs_umount(fs);
	return 0;
}
