#include "devnode.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BFREE_KERNEL_GUEST
#include "debugcon.h"
#endif

static struct bfree_vnode *ensure_dev_dir(struct bfree_fs *fs)
{
	size_t i;

	for (i = 0; i < fs->root.child_count; i++) {
		if (strcmp(fs->root.children[i]->name, "dev") == 0)
			return fs->root.children[i];
	}
	if (fs->root.child_count >= BFREE_MAX_CHILD)
		return NULL;
	{
		struct bfree_vnode *dev = calloc(1, sizeof(*dev));

		if (dev == NULL)
			return NULL;
		snprintf(dev->name, sizeof(dev->name), "dev");
		dev->type = BFREE_VNODE_DIR;
		dev->parent = &fs->root;
		fs->root.children[fs->root.child_count++] = dev;
		return dev;
	}
}

static int add_dev(struct bfree_vnode *dir, const char *name, uint32_t dev_id)
{
	struct bfree_vnode *vn;
	size_t i;

	for (i = 0; i < dir->child_count; i++) {
		if (strcmp(dir->children[i]->name, name) == 0)
			return 0;
	}
	if (dir->child_count >= BFREE_MAX_CHILD)
		return -ENOSPC;
	vn = calloc(1, sizeof(*vn));
	if (vn == NULL)
		return -ENOMEM;
	snprintf(vn->name, sizeof(vn->name), "%s", name);
	vn->type = BFREE_VNODE_DEV;
	vn->dev_id = dev_id;
	vn->parent = dir;
	dir->children[dir->child_count++] = vn;
	return 0;
}

void bfree_devnodes_init(struct bfree_fs *fs)
{
	struct bfree_vnode *dev;

	if (fs == NULL)
		return;
	dev = ensure_dev_dir(fs);
	if (dev == NULL)
		return;
	add_dev(dev, "null", BFREE_DEV_NULL);
	add_dev(dev, "zero", BFREE_DEV_ZERO);
	add_dev(dev, "console", BFREE_DEV_CONSOLE);
}

ssize_t bfree_dev_read(struct bfree_fs *fs, int fd, void *buf, size_t count)
{
	struct bfree_ofd *ofd;

	ofd = bfree_ofd_for_fd(fs, fd);
	if (ofd == NULL || ofd->vnode->type != BFREE_VNODE_DEV)
		return -EBADF;
	if (ofd->vnode->dev_id == BFREE_DEV_NULL)
		return 0;
	if (ofd->vnode->dev_id == BFREE_DEV_ZERO) {
		memset(buf, 0, count);
		return (ssize_t)count;
	}
	return -EINVAL;
}

ssize_t bfree_dev_write(struct bfree_fs *fs, int fd, const void *buf,
			size_t count)
{
	struct bfree_ofd *ofd;

	(void)buf;
	ofd = bfree_ofd_for_fd(fs, fd);
	if (ofd == NULL || ofd->vnode->type != BFREE_VNODE_DEV)
		return -EBADF;
	if (ofd->vnode->dev_id == BFREE_DEV_NULL ||
	    ofd->vnode->dev_id == BFREE_DEV_ZERO)
		return (ssize_t)count;
	if (ofd->vnode->dev_id == BFREE_DEV_CONSOLE) {
#ifdef BFREE_KERNEL_GUEST
		bfree_debug_write(buf, count);
#endif
		return (ssize_t)count;
	}
	return -EINVAL;
}
