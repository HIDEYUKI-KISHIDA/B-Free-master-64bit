/*
 * Synthetic vnode FS with per-OFD directory cursors (M1 milestone).
 */
#include "fs_ofd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

static int path_is_absolute(const char *path)
{
	return path != NULL && path[0] == '/';
}

static struct bfree_vnode *vnode_alloc(const char *name, bfree_vtype_t type,
				       struct bfree_vnode *parent)
{
	struct bfree_vnode *vn;

	vn = calloc(1, sizeof(*vn));
	if (vn == NULL)
		return NULL;
	snprintf(vn->name, sizeof(vn->name), "%s", name);
	vn->type = type;
	vn->parent = parent;
	return vn;
}

static int vnode_add_child(struct bfree_vnode *dir, struct bfree_vnode *child)
{
	if (dir->type != BFREE_VNODE_DIR || dir->child_count >= BFREE_MAX_CHILD)
		return -ENOSPC;
	dir->children[dir->child_count++] = child;
	child->parent = dir;
	return 0;
}

static struct bfree_vnode *vnode_find_child(struct bfree_vnode *dir,
					    const char *name)
{
	size_t i;

	for (i = 0; i < dir->child_count; i++) {
		if (strcmp(dir->children[i]->name, name) == 0)
			return dir->children[i];
	}
	return NULL;
}

static void ensure_tmp_hierarchy(struct bfree_fs *fs)
{
	struct bfree_vnode *tmp;
	struct bfree_vnode *var;
	struct bfree_vnode *run;

	if (vnode_find_child(&fs->root, "tmp") != NULL)
		return;

	tmp = vnode_alloc("tmp", BFREE_VNODE_DIR, &fs->root);
	var = vnode_alloc("var", BFREE_VNODE_DIR, tmp);
	run = vnode_alloc("run", BFREE_VNODE_DIR, var);
	if (tmp == NULL || var == NULL || run == NULL)
		return;

	vnode_add_child(&fs->root, tmp);
	vnode_add_child(tmp, var);
	vnode_add_child(var, run);
}

void bfree_fs_init(struct bfree_fs *fs)
{
	size_t i;

	memset(fs, 0, sizeof(*fs));
	snprintf(fs->root.name, sizeof(fs->root.name), "/");
	fs->root.type = BFREE_VNODE_DIR;
	for (i = 0; i < BFREE_MAX_FD; i++)
		fs->fd_ofd[i] = -1;
	ensure_tmp_hierarchy(fs);
}

static int alloc_ofd_slot(struct bfree_fs *fs)
{
	int i;

	for (i = 0; i < BFREE_MAX_OFD; i++) {
		int idx = (fs->next_ofd + i) % BFREE_MAX_OFD;
		if (fs->ofd_table[idx].refcount == 0) {
			fs->next_ofd = (idx + 1) % BFREE_MAX_OFD;
			return idx;
		}
	}
	return -1;
}

static int alloc_fd(struct bfree_fs *fs, int ofd_idx)
{
	int fd;

	for (fd = 0; fd < BFREE_MAX_FD; fd++) {
		if (fs->fd_ofd[fd] < 0) {
			fs->fd_ofd[fd] = ofd_idx;
			return fd;
		}
	}
	return -1;
}

static struct bfree_ofd *ofd_from_fd(struct bfree_fs *fs, int fd)
{
	int idx;

	if (fd < 0 || fd >= BFREE_MAX_FD || fs->fd_ofd[fd] < 0)
		return NULL;
	idx = fs->fd_ofd[fd];
	if (idx < 0 || idx >= BFREE_MAX_OFD)
		return NULL;
	if (fs->ofd_table[idx].refcount == 0)
		return NULL;
	return &fs->ofd_table[idx];
}

struct bfree_ofd *bfree_ofd_for_fd(struct bfree_fs *fs, int fd)
{
	return ofd_from_fd(fs, fd);
}

static struct bfree_vnode *resolve_parent(struct bfree_fs *fs,
					  const char *path,
					  char *leaf,
					  size_t leaf_sz)
{
	struct bfree_vnode *cur;
	const char *p;
	char component[BFREE_MAX_NAME];

	if (!path_is_absolute(path))
		return NULL;
	if (strcmp(path, "/") == 0) {
		snprintf(leaf, leaf_sz, "/");
		return &fs->root;
	}

	cur = &fs->root;
	p = path;
	while (*p == '/')
		p++;

	for (;;) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);

		if (len == 0)
			break;
		if (len >= sizeof(component))
			return NULL;
		memcpy(component, p, len);
		component[len] = '\0';

		if (slash == NULL) {
			snprintf(leaf, leaf_sz, "%s", component);
			return cur;
		}

		cur = vnode_find_child(cur, component);
		if (cur == NULL || cur->type != BFREE_VNODE_DIR)
			return NULL;
		p = slash + 1;
		while (*p == '/')
			p++;
	}

	return NULL;
}

struct bfree_vnode *bfree_lookup(struct bfree_fs *fs, const char *path)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;

	if (strcmp(path, "/") == 0)
		return &fs->root;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL)
		return NULL;
	if (strcmp(leaf, "/") == 0)
		return parent;
	return vnode_find_child(parent, leaf);
}

static int ofd_open_vnode(struct bfree_fs *fs, struct bfree_vnode *vn,
			  int flags)
{
	int ofd_idx;
	int fd;
	struct bfree_ofd *ofd;

	(void)flags;
	ofd_idx = alloc_ofd_slot(fs);
	if (ofd_idx < 0)
		return -EMFILE;

	ofd = &fs->ofd_table[ofd_idx];
	memset(ofd, 0, sizeof(*ofd));
	ofd->vnode = vn;
	ofd->offset = 0;
	ofd->dirent_index = 0;
	ofd->refcount = 1;

	fd = alloc_fd(fs, ofd_idx);
	if (fd < 0) {
		ofd->refcount = 0;
		return -EMFILE;
	}
	return fd;
}

int bfree_open(struct bfree_fs *fs, const char *path, int flags, int mode)
{
	struct bfree_vnode *vn;

	(void)mode;
	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	return ofd_open_vnode(fs, vn, flags);
}

int bfree_dup(struct bfree_fs *fs, int fd)
{
	struct bfree_ofd *ofd;
	int ofd_idx;
	int new_fd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	ofd_idx = fs->fd_ofd[fd];
	ofd->refcount++;
	new_fd = alloc_fd(fs, ofd_idx);
	if (new_fd < 0) {
		ofd->refcount--;
		return -EMFILE;
	}
	return new_fd;
}

int bfree_close(struct bfree_fs *fs, int fd)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;

	fs->fd_ofd[fd] = -1;
	if (--ofd->refcount == 0)
		memset(ofd, 0, sizeof(*ofd));
	return 0;
}

ssize_t bfree_read(struct bfree_fs *fs, int fd, void *buf, size_t count)
{
	struct bfree_ofd *ofd;
	size_t avail;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (ofd->vnode->type != BFREE_VNODE_FILE)
		return -EISDIR;
	if (ofd->offset < 0 || (size_t)ofd->offset > ofd->vnode->size)
		return -EINVAL;

	avail = ofd->vnode->size - (size_t)ofd->offset;
	if (count > avail)
		count = avail;
	memcpy(buf, ofd->vnode->data + ofd->offset, count);
	ofd->offset += (off_t)count;
	return (ssize_t)count;
}

ssize_t bfree_write(struct bfree_fs *fs, int fd, const void *buf, size_t count)
{
	struct bfree_ofd *ofd;
	size_t new_size;
	char *new_data;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (ofd->vnode->type != BFREE_VNODE_FILE)
		return -EISDIR;
	if (ofd->offset < 0)
		return -EINVAL;

	new_size = (size_t)ofd->offset + count;
	if (new_size > ofd->vnode->size) {
		new_data = realloc(ofd->vnode->data, new_size);
		if (new_data == NULL)
			return -ENOMEM;
		memset(new_data + ofd->vnode->size, 0, new_size - ofd->vnode->size);
		ofd->vnode->data = new_data;
		ofd->vnode->size = new_size;
	}
	memcpy(ofd->vnode->data + ofd->offset, buf, count);
	ofd->offset += (off_t)count;
	return (ssize_t)count;
}

off_t bfree_lseek(struct bfree_fs *fs, int fd, off_t offset, int whence)
{
	struct bfree_ofd *ofd;
	off_t new_off;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (ofd->vnode->type != BFREE_VNODE_FILE)
		return -ESPIPE;

	switch (whence) {
	case 0: /* SEEK_SET */
		new_off = offset;
		break;
	case 1: /* SEEK_CUR */
		new_off = ofd->offset + offset;
		break;
	case 2: /* SEEK_END */
		new_off = (off_t)ofd->vnode->size + offset;
		break;
	default:
		return -EINVAL;
	}
	if (new_off < 0)
		return -EINVAL;
	ofd->offset = new_off;
	return ofd->offset;
}

int bfree_mkdir(struct bfree_fs *fs, const char *path, int mode)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

	(void)mode;
	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL || parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;

	child = vnode_alloc(leaf, BFREE_VNODE_DIR, parent);
	if (child == NULL)
		return -ENOMEM;
	return vnode_add_child(parent, child);
}

int bfree_rmdir(struct bfree_fs *fs, const char *path)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;
	size_t i;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL)
		return -ENOENT;
	child = vnode_find_child(parent, leaf);
	if (child == NULL)
		return -ENOENT;
	if (child->type != BFREE_VNODE_DIR)
		return -ENOTDIR;
	if (child->child_count != 0)
		return -ENOTEMPTY;

	for (i = 0; i < parent->child_count; i++) {
		if (parent->children[i] == child) {
			parent->children[i] =
				parent->children[parent->child_count - 1];
			parent->child_count--;
			free(child);
			return 0;
		}
	}
	return -ENOENT;
}

static unsigned char dirent_type(struct bfree_vnode *vn)
{
	return (vn->type == BFREE_VNODE_DIR) ? DT_DIR : DT_REG;
}

ssize_t bfree_getdents64(struct bfree_fs *fs, int fd, void *buf, size_t count)
{
	struct bfree_ofd *ofd;
	struct bfree_vnode *dir;
	unsigned char *out;
	size_t out_used;
	size_t idx;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	dir = ofd->vnode;
	if (dir->type != BFREE_VNODE_DIR)
		return -ENOTDIR;
	if (count < sizeof(struct bfree_linux_dirent64) + 2)
		return -EINVAL;

	out = buf;
	out_used = 0;
	idx = ofd->dirent_index;

	while (idx < dir->child_count) {
		struct bfree_vnode *child = dir->children[idx];
		size_t namelen = strlen(child->name) + 1;
		size_t reclen = (sizeof(struct bfree_linux_dirent64) + namelen + 7) & ~7;
		struct bfree_linux_dirent64 *de;

		if (out_used + reclen > count)
			break;

		de = (struct bfree_linux_dirent64 *)(out + out_used);
		de->d_ino = (uint64_t)(idx + 2);
		de->d_off = (int64_t)(idx + 1);
		de->d_reclen = (unsigned short)reclen;
		de->d_type = dirent_type(child);
		memcpy(de->d_name, child->name, namelen);
		out_used += reclen;
		idx++;
	}

	ofd->dirent_index = idx;
	return (ssize_t)out_used;
}
