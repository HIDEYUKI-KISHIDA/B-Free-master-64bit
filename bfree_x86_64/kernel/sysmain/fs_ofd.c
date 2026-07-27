/*
 * Synthetic vnode FS: per-OFD dirent cursors, unlink-while-open, *at syscalls.
 */
#include "cred.h"
#include "devnode.h"
#include "fs_ofd.h"
#include "blk_persist.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

#define AT_REMOVEDIR 0x200

#ifndef O_CREAT
#define O_CREAT 0100
#endif
#ifndef O_EXCL
#define O_EXCL 0200
#endif
#ifndef O_TRUNC
#define O_TRUNC 01000
#endif
#ifndef O_APPEND
#define O_APPEND 02000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 04000
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef F_OK
#define F_OK 0
#endif

static unsigned apply_umask_mode(struct bfree_fs *fs, unsigned mode,
				 unsigned typebits)
{
	unsigned mask = fs != NULL ? fs->umask : 0022u;

	return typebits | ((mode & 0777u) & ~mask);
}

static int vnode_check_access(const struct bfree_vnode *vn, int mode)
{
	unsigned perms;
	unsigned need = 0;

	if (vn == NULL)
		return -ENOENT;
	if (mode == F_OK)
		return 0;
	if (mode & ~(R_OK | W_OK | X_OK))
		return -EINVAL;

	/* Owner triad when uid matches (incl. root-owned files as uid 0). */
	if (vn->uid == bfree_getuid())
		perms = (vn->mode >> 6) & 7u;
	else if (vn->gid == bfree_getgid())
		perms = (vn->mode >> 3) & 7u;
	else
		perms = vn->mode & 7u;

	/* Root may read/write anything; execute still needs an x bit. */
	if (bfree_getuid() == 0) {
		if ((mode & X_OK) && ((vn->mode & 0111u) == 0))
			return -EACCES;
		return 0;
	}

	if (mode & R_OK)
		need |= 4u;
	if (mode & W_OK)
		need |= 2u;
	if (mode & X_OK)
		need |= 1u;
	if ((perms & need) != need)
		return -EACCES;
	return 0;
}

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
	vn->nlink = 1;
	if (type == BFREE_VNODE_DIR)
		vn->mode = 0040755u;
	else if (type == BFREE_VNODE_LNK)
		vn->mode = 0120777u;
	else if (type == BFREE_VNODE_DEV)
		vn->mode = 0020666u;
	else
		vn->mode = 0100644u;
	return vn;
}

static void vnode_hold(struct bfree_vnode *vn)
{
	if (vn != NULL)
		vn->nref++;
}

static void vnode_free(struct bfree_fs *fs, struct bfree_vnode *vn)
{
	if (vn == NULL || vn == &fs->root)
		return;
	if (fs->mounted)
		bfree_blk_file_free(fs, vn);
	free(vn->data);
	free(vn);
}

static void vnode_rele(struct bfree_fs *fs, struct bfree_vnode *vn)
{
	if (vn == NULL)
		return;
	if (vn->nref == 0)
		return;
	if (--vn->nref == 0 && vn->unlinked)
		vnode_free(fs, vn);
}

int bfree_vnode_is_alive(struct bfree_vnode *vn)
{
	return vn != NULL && vn->nref > 0;
}

static int vnode_add_child(struct bfree_vnode *dir, struct bfree_vnode *child)
{
	if (dir->type != BFREE_VNODE_DIR || dir->child_count >= BFREE_MAX_CHILD)
		return -ENOSPC;
	dir->children[dir->child_count++] = child;
	child->parent = dir;
	return 0;
}

static int vnode_remove_child(struct bfree_vnode *parent, struct bfree_vnode *child)
{
	size_t i;

	for (i = 0; i < parent->child_count; i++) {
		if (parent->children[i] == child) {
			parent->children[i] =
				parent->children[parent->child_count - 1];
			parent->child_count--;
			child->parent = NULL;
			return 0;
		}
	}
	return -ENOENT;
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
	fs->root.ino = 1;
	fs->root.nlink = 1;
	fs->root.mode = 0040755u;
	fs->cwd = &fs->root;
	fs->fd_ofd = fs->fd_ofd_store;
	fs->fd_flags = fs->fd_flags_store;
	fs->umask = 0022;
	for (i = 0; i < BFREE_MAX_FD; i++) {
		fs->fd_ofd[i] = -1;
		fs->fd_flags[i] = 0;
	}
	if (!fs->mounted)
		ensure_tmp_hierarchy(fs);
	bfree_devnodes_init(fs);
}

void bfree_fs_bind_fd_table(struct bfree_fs *fs, int *fd_ofd, int *fd_flags)
{
	if (fs == NULL)
		return;
	fs->fd_ofd = fd_ofd != NULL ? fd_ofd : fs->fd_ofd_store;
	fs->fd_flags = fd_flags != NULL ? fd_flags : fs->fd_flags_store;
}

void bfree_fs_init_proc_fds(int *fd_ofd, int *fd_flags)
{
	int i;

	if (fd_ofd == NULL || fd_flags == NULL)
		return;
	for (i = 0; i < BFREE_MAX_FD; i++) {
		fd_ofd[i] = -1;
		fd_flags[i] = 0;
	}
}

int bfree_fs_fork_fds(struct bfree_fs *fs, int *dst_ofd, int *dst_flags,
		      const int *src_ofd, const int *src_flags)
{
	int i;

	if (fs == NULL || dst_ofd == NULL || dst_flags == NULL ||
	    src_ofd == NULL || src_flags == NULL)
		return -EINVAL;
	for (i = 0; i < BFREE_MAX_FD; i++) {
		dst_ofd[i] = src_ofd[i];
		dst_flags[i] = src_flags[i];
		if (src_ofd[i] >= 0 && src_ofd[i] < BFREE_MAX_OFD &&
		    fs->ofd_table[src_ofd[i]].refcount > 0)
			fs->ofd_table[src_ofd[i]].refcount++;
	}
	return 0;
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

static struct bfree_vnode *dirfd_vnode(struct bfree_fs *fs, int dirfd)
{
	struct bfree_ofd *ofd;

	if (dirfd == BFREE_AT_FDCWD)
		return fs->cwd != NULL ? fs->cwd : &fs->root;
	ofd = ofd_from_fd(fs, dirfd);
	if (ofd == NULL)
		return NULL;
	if (ofd->vnode->type != BFREE_VNODE_DIR)
		return NULL;
	return ofd->vnode;
}

static struct bfree_vnode *lookup_component(struct bfree_vnode *dir,
					    const char *name)
{
	if (strcmp(name, ".") == 0)
		return dir;
	if (strcmp(name, "..") == 0)
		return dir->parent != NULL ? dir->parent : dir;
	return vnode_find_child(dir, name);
}

static struct bfree_vnode *lookup_from_dir(struct bfree_vnode *start,
					   const char *path)
{
	struct bfree_vnode *cur = start;
	const char *p;
	char component[BFREE_MAX_NAME];

	if (start == NULL || path == NULL)
		return NULL;
	if (path[0] == '\0')
		return start;

	p = path;
	while (*p == '/')
		p++;

	for (;;) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);

		if (len == 0) {
			if (slash == NULL)
				return cur;
			p = slash + 1;
			while (*p == '/')
				p++;
			continue;
		}
		if (len >= sizeof(component))
			return NULL;
		memcpy(component, p, len);
		component[len] = '\0';

		cur = lookup_component(cur, component);
		if (cur == NULL)
			return NULL;

		if (slash == NULL)
			return cur;
		if (cur->type != BFREE_VNODE_DIR)
			return NULL;
		p = slash + 1;
		while (*p == '/')
			p++;
	}
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

static int resolve_parent_at(struct bfree_fs *fs, int dirfd, const char *path,
			     struct bfree_vnode **parent_out, char *leaf,
			     size_t leaf_sz)
{
	const char *p = path;
	char component[BFREE_MAX_NAME];
	struct bfree_vnode *cur;
	struct bfree_vnode *base;

	if (path_is_absolute(path)) {
		*parent_out = resolve_parent(fs, path, leaf, leaf_sz);
		return (*parent_out != NULL) ? 0 : -ENOENT;
	}

	base = dirfd_vnode(fs, dirfd);
	if (base == NULL)
		return -EBADF;
	if (path[0] == '\0')
		return -ENOENT;

	cur = base;
	p = path;
	while (*p == '/')
		p++;

	for (;;) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);

		if (len == 0)
			break;
		if (len >= sizeof(component))
			return -ENAMETOOLONG;
		memcpy(component, p, len);
		component[len] = '\0';

		if (slash == NULL) {
			snprintf(leaf, leaf_sz, "%s", component);
			*parent_out = cur;
			return 0;
		}

		cur = lookup_component(cur, component);
		if (cur == NULL)
			return -ENOENT;
		if (cur->type != BFREE_VNODE_DIR)
			return -ENOTDIR;
		p = slash + 1;
		while (*p == '/')
			p++;
	}

	return -ENOENT;
}

struct bfree_vnode *bfree_lookup(struct bfree_fs *fs, const char *path)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;

	if (path == NULL)
		return NULL;
	if (!path_is_absolute(path))
		return lookup_from_dir(fs->cwd != NULL ? fs->cwd : &fs->root,
				       path);
	if (strcmp(path, "/") == 0)
		return &fs->root;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL)
		return NULL;
	if (strcmp(leaf, "/") == 0)
		return parent;
	return vnode_find_child(parent, leaf);
}

static struct bfree_vnode *lookup_at(struct bfree_fs *fs, int dirfd,
				     const char *path)
{
	if (path_is_absolute(path))
		return bfree_lookup(fs, path);

	return lookup_from_dir(dirfd_vnode(fs, dirfd), path);
}

static int ofd_open_vnode(struct bfree_fs *fs, struct bfree_vnode *vn,
			  int flags)
{
	int ofd_idx;
	int fd;
	struct bfree_ofd *ofd;

	ofd_idx = alloc_ofd_slot(fs);
	if (ofd_idx < 0)
		return -EMFILE;

	ofd = &fs->ofd_table[ofd_idx];
	memset(ofd, 0, sizeof(*ofd));
	ofd->vnode = vn;
	ofd->offset = 0;
	ofd->dirent_index = 0;
	ofd->flags = flags;
	ofd->refcount = 1;
	vnode_hold(vn);

	if ((flags & O_TRUNC) && vn->type == BFREE_VNODE_FILE) {
		vn->size = 0;
		if (vn->data != NULL) {
			free(vn->data);
			vn->data = NULL;
		}
	}

	fd = alloc_fd(fs, ofd_idx);
	if (fd < 0) {
		vnode_rele(fs, vn);
		ofd->refcount = 0;
		return -EMFILE;
	}
	return fd;
}

int bfree_open(struct bfree_fs *fs, const char *path, int flags, int mode)
{
	struct bfree_vnode *vn;
	int rc;

	vn = bfree_lookup(fs, path);
	if (vn == NULL) {
		if (!(flags & O_CREAT))
			return -ENOENT;
		rc = bfree_create(fs, path, mode);
		if (rc < 0)
			return rc;
		vn = bfree_lookup(fs, path);
		if (vn == NULL)
			return -ENOENT;
	} else if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
		return -EEXIST;
	}
	return ofd_open_vnode(fs, vn, flags);
}

int bfree_openat(struct bfree_fs *fs, int dirfd, const char *path,
		 int flags, int mode)
{
	struct bfree_vnode *vn;
	int rc;

	if (path == NULL)
		return -EINVAL;
	if (path_is_absolute(path))
		return bfree_open(fs, path, flags, mode);

	vn = lookup_at(fs, dirfd, path);
	if (vn == NULL) {
		char leaf[BFREE_MAX_NAME];
		struct bfree_vnode *parent;
		struct bfree_vnode *child;

		if (!(flags & O_CREAT))
			return -ENOENT;
		rc = resolve_parent_at(fs, dirfd, path, &parent, leaf,
				       sizeof(leaf));
		if (rc < 0)
			return rc;
		if (parent->type != BFREE_VNODE_DIR)
			return -ENOTDIR;
		if (vnode_find_child(parent, leaf) != NULL) {
			if (flags & O_EXCL)
				return -EEXIST;
			vn = vnode_find_child(parent, leaf);
			return ofd_open_vnode(fs, vn, flags);
		}
		child = vnode_alloc(leaf, BFREE_VNODE_FILE, parent);
		if (child == NULL)
			return -ENOMEM;
		child->mode = apply_umask_mode(fs, (unsigned)mode, 0100000u);
		child->uid = bfree_getuid();
		child->gid = bfree_getgid();
		rc = vnode_add_child(parent, child);
		if (rc < 0)
			return rc;
		return ofd_open_vnode(fs, child, flags);
	}
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return -EEXIST;
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
	fs->fd_flags[new_fd] = fs->fd_flags[fd];
	return new_fd;
}

int bfree_dup2(struct bfree_fs *fs, int oldfd, int newfd)
{
	struct bfree_ofd *ofd;
	int ofd_idx;
	int flags;

	if (oldfd < 0 || oldfd >= BFREE_MAX_FD ||
	    newfd < 0 || newfd >= BFREE_MAX_FD)
		return -EBADF;
	if (oldfd == newfd)
		return newfd;
	ofd = ofd_from_fd(fs, oldfd);
	if (ofd == NULL)
		return -EBADF;
	ofd_idx = fs->fd_ofd[oldfd];
	flags = fs->fd_flags[oldfd];
	if (fs->fd_ofd[newfd] >= 0)
		bfree_close(fs, newfd);
	ofd->refcount++;
	fs->fd_ofd[newfd] = ofd_idx;
	fs->fd_flags[newfd] = flags;
	return newfd;
}

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

int bfree_fcntl(struct bfree_fs *fs, int fd, int cmd, long arg)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	switch (cmd) {
	case F_GETFD:
		return (fs->fd_flags[fd] & BFREE_FD_CLOEXEC) ? FD_CLOEXEC : 0;
	case F_SETFD:
		fs->fd_flags[fd] = (arg & FD_CLOEXEC) ? BFREE_FD_CLOEXEC : 0;
		return 0;
	case F_GETFL:
		return (int)ofd->flags;
	case F_SETFL:
		ofd->flags = (ofd->flags & ~(O_NONBLOCK | O_APPEND)) |
			     (arg & (O_NONBLOCK | O_APPEND));
		return 0;
	default:
		return -EINVAL;
	}
}

int bfree_chdir(struct bfree_fs *fs, const char *path)
{
	struct bfree_vnode *vn;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	if (vn->type != BFREE_VNODE_DIR)
		return -ENOTDIR;
	fs->cwd = vn;
	return 0;
}

int bfree_getcwd(struct bfree_fs *fs, char *buf, size_t size)
{
	struct bfree_vnode *vn;
	char stack[BFREE_MAX_PATH][BFREE_MAX_NAME];
	int depth = 0;
	size_t len;
	int i;

	if (buf == NULL || size == 0)
		return -EINVAL;
	vn = fs->cwd != NULL ? fs->cwd : &fs->root;
	if (vn == &fs->root) {
		if (size < 2)
			return -ERANGE;
		buf[0] = '/';
		buf[1] = '\0';
		return 0;
	}
	while (vn != NULL && vn != &fs->root && depth < BFREE_MAX_PATH) {
		snprintf(stack[depth], sizeof(stack[depth]), "%s", vn->name);
		depth++;
		vn = vn->parent;
	}
	len = 1;
	for (i = depth - 1; i >= 0; i--) {
		size_t n = strlen(stack[i]) + 1;

		if (len + n >= size)
			return -ERANGE;
		buf[len - 1] = '/';
		memcpy(buf + len, stack[i], n);
		len += n;
	}
	if (len == 1) {
		buf[0] = '/';
		buf[1] = '\0';
	} else {
		buf[len - 1] = '\0';
	}
	return 0;
}

int bfree_close(struct bfree_fs *fs, int fd)
{
	struct bfree_ofd *ofd;
	struct bfree_vnode *vn;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;

	vn = ofd->vnode;
	fs->fd_ofd[fd] = -1;
	if (--ofd->refcount == 0) {
		vnode_rele(fs, vn);
		memset(ofd, 0, sizeof(*ofd));
	}
	return 0;
}

ssize_t bfree_read(struct bfree_fs *fs, int fd, void *buf, size_t count)
{
	struct bfree_ofd *ofd;
	size_t avail;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (ofd->vnode->type == BFREE_VNODE_DEV)
		return bfree_dev_read(fs, fd, buf, count);
	if (ofd->vnode->type != BFREE_VNODE_FILE)
		return -EISDIR;
	if (ofd->offset < 0 || (size_t)ofd->offset > ofd->vnode->size)
		return -EINVAL;

	avail = ofd->vnode->size - (size_t)ofd->offset;
	if (count > avail)
		count = avail;
	if (fs->mounted) {
		ssize_t n = bfree_blk_file_read(fs, ofd->vnode, ofd->offset, buf,
						count);
		if (n < 0)
			return n;
		ofd->offset += n;
		return n;
	}
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
	if (ofd->vnode->type == BFREE_VNODE_DEV)
		return bfree_dev_write(fs, fd, buf, count);
	if (ofd->vnode->type != BFREE_VNODE_FILE)
		return -EISDIR;
	if (ofd->offset < 0)
		return -EINVAL;

	new_size = (size_t)ofd->offset + count;
	if (fs->mounted) {
		ssize_t n = bfree_blk_file_write(fs, ofd->vnode, ofd->offset, buf,
						 count);
		if (n < 0)
			return n;
		ofd->offset += n;
		return n;
	}
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
	case 0:
		new_off = offset;
		break;
	case 1:
		new_off = ofd->offset + offset;
		break;
	case 2:
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

int bfree_create(struct bfree_fs *fs, const char *path, int mode)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL || parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;

	child = vnode_alloc(leaf, BFREE_VNODE_FILE, parent);
	if (child == NULL)
		return -ENOMEM;
	child->mode = apply_umask_mode(fs, (unsigned)mode, 0100000u);
	child->uid = bfree_getuid();
	child->gid = bfree_getgid();
	return vnode_add_child(parent, child);
}

int bfree_mkdir(struct bfree_fs *fs, const char *path, int mode)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL || parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;

	child = vnode_alloc(leaf, BFREE_VNODE_DIR, parent);
	if (child == NULL)
		return -ENOMEM;
	child->mode = apply_umask_mode(fs, (unsigned)mode, 0040000u);
	child->uid = bfree_getuid();
	child->gid = bfree_getgid();
	return vnode_add_child(parent, child);
}

int bfree_mkdirat(struct bfree_fs *fs, int dirfd, const char *path, int mode)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;
	int rc;

	if (path_is_absolute(path))
		return bfree_mkdir(fs, path, mode);

	rc = resolve_parent_at(fs, dirfd, path, &parent, leaf, sizeof(leaf));
	if (rc < 0)
		return rc;
	if (parent->type != BFREE_VNODE_DIR)
		return -ENOTDIR;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;

	child = vnode_alloc(leaf, BFREE_VNODE_DIR, parent);
	if (child == NULL)
		return -ENOMEM;
	child->mode = apply_umask_mode(fs, (unsigned)mode, 0040000u);
	child->uid = bfree_getuid();
	child->gid = bfree_getgid();
	return vnode_add_child(parent, child);
}

static int vnode_unlink(struct bfree_fs *fs, struct bfree_vnode *parent,
			struct bfree_vnode *child)
{
	if (child->type == BFREE_VNODE_DIR)
		return -EISDIR;
	if (vnode_remove_child(parent, child) < 0)
		return -ENOENT;
	if (child->nlink > 0)
		child->nlink--;
	if (child->nlink == 0)
		child->unlinked = 1;
	if (child->nlink == 0 && child->nref == 0)
		vnode_free(fs, child);
	return 0;
}

int bfree_unlink(struct bfree_fs *fs, const char *path)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

	parent = resolve_parent(fs, path, leaf, sizeof(leaf));
	if (parent == NULL)
		return -ENOENT;
	child = vnode_find_child(parent, leaf);
	if (child == NULL)
		return -ENOENT;
	return vnode_unlink(fs, parent, child);
}

int bfree_unlinkat(struct bfree_fs *fs, int dirfd, const char *path, int flags)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;
	int rc;

	if (path_is_absolute(path))
		return bfree_unlink(fs, path);

	rc = resolve_parent_at(fs, dirfd, path, &parent, leaf, sizeof(leaf));
	if (rc < 0)
		return rc;
	child = vnode_find_child(parent, leaf);
	if (child == NULL)
		return -ENOENT;

	if (flags & AT_REMOVEDIR) {
		if (child->type != BFREE_VNODE_DIR)
			return -ENOTDIR;
		if (child->child_count != 0)
			return -ENOTEMPTY;
		if (vnode_remove_child(parent, child) < 0)
			return -ENOENT;
		child->unlinked = 1;
		if (child->nref == 0)
			vnode_free(fs, child);
		return 0;
	}
	return vnode_unlink(fs, parent, child);
}

int bfree_rmdir(struct bfree_fs *fs, const char *path)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

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

	if (vnode_remove_child(parent, child) < 0)
		return -ENOENT;
	child->unlinked = 1;
	if (child->nref == 0)
		vnode_free(fs, child);
	return 0;
}

static unsigned char dirent_type(struct bfree_vnode *vn)
{
	if (vn->type == BFREE_VNODE_DIR)
		return DT_DIR;
	if (vn->type == BFREE_VNODE_LNK)
		return 10; /* DT_LNK */
	return DT_REG;
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

	while (idx < dir->child_count + 2U) {
		const char *name;
		unsigned char dtype;
		uint64_t ino;
		size_t namelen;
		size_t reclen;
		struct bfree_linux_dirent64 *de;

		if (idx == 0) {
			name = ".";
			dtype = DT_DIR;
			ino = 1;
		} else if (idx == 1) {
			name = "..";
			dtype = DT_DIR;
			ino = 1;
		} else {
			struct bfree_vnode *child = dir->children[idx - 2U];

			name = child->name;
			dtype = dirent_type(child);
			ino = (uint64_t)(idx);
		}
		namelen = strlen(name) + 1;
		reclen = (sizeof(struct bfree_linux_dirent64) + namelen + 7) &
			 ~7ULL;
		if (out_used + reclen > count)
			break;

		de = (struct bfree_linux_dirent64 *)(out + out_used);
		de->d_ino = ino;
		de->d_off = (int64_t)(idx + 1);
		de->d_reclen = (unsigned short)reclen;
		de->d_type = dtype;
		memcpy(de->d_name, name, namelen);
		out_used += reclen;
		idx++;
	}

	ofd->dirent_index = idx;
	return (ssize_t)out_used;
}

void bfree_vnode_fill_stat(const struct bfree_vnode *vn,
			   struct bfree_linux_stat *st)
{
	uint64_t ino;

	if (vn == NULL || st == NULL)
		return;

	memset(st, 0, sizeof(*st));
	ino = vn->ino;
	if (ino == 0)
		ino = (uint64_t)((uintptr_t)vn & 0xffffu) + 2u;
	st->st_ino = ino;
	st->st_nlink = vn->nlink ? vn->nlink : 1;
	st->st_uid = vn->uid;
	st->st_gid = vn->gid;
	st->st_blksize = 4096;
	st->st_blocks = (vn->size + 511) / 512;
	st->st_mtime_sec = vn->mtime_sec;
	st->st_mtime_nsec = vn->mtime_nsec;
	st->st_atime_sec = vn->mtime_sec;
	st->st_ctime_sec = vn->mtime_sec;

	if (vn->mode != 0) {
		st->st_mode = vn->mode;
		st->st_size = (int64_t)vn->size;
		if (vn->type == BFREE_VNODE_DEV)
			st->st_rdev = (uint64_t)vn->dev_id;
		return;
	}

	switch (vn->type) {
	case BFREE_VNODE_FILE:
		st->st_mode = 0100644u;
		st->st_size = (int64_t)vn->size;
		break;
	case BFREE_VNODE_DIR:
		st->st_mode = 0040755u;
		break;
	case BFREE_VNODE_DEV:
		st->st_mode = 0020666u;
		st->st_rdev = (uint64_t)vn->dev_id;
		break;
	case BFREE_VNODE_LNK:
		st->st_mode = 0120777u;
		st->st_size = (int64_t)vn->size;
		break;
	default:
		st->st_mode = 0100644u;
		break;
	}
}

int bfree_stat(struct bfree_fs *fs, const char *path,
	       struct bfree_linux_stat *st)
{
	struct bfree_vnode *vn;

	if (st == NULL)
		return -EFAULT;
	if (path == NULL)
		return -EFAULT;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	bfree_vnode_fill_stat(vn, st);
	return 0;
}

int bfree_fstat(struct bfree_fs *fs, int fd, struct bfree_linux_stat *st)
{
	struct bfree_ofd *ofd;

	if (st == NULL)
		return -EFAULT;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	bfree_vnode_fill_stat(ofd->vnode, st);
	return 0;
}

int bfree_fstatat(struct bfree_fs *fs, int dirfd, const char *path,
		  struct bfree_linux_stat *st, int flags)
{
	struct bfree_vnode *vn;

	(void)flags;
	if (st == NULL)
		return -EFAULT;
	if (path == NULL)
		return -EFAULT;

	if (path_is_absolute(path))
		vn = bfree_lookup(fs, path);
	else
		vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	bfree_vnode_fill_stat(vn, st);
	return 0;
}

unsigned bfree_umask(struct bfree_fs *fs, unsigned mask)
{
	unsigned old;

	if (fs == NULL)
		return 0;
	old = fs->umask;
	fs->umask = mask & 0777u;
	return old;
}

int bfree_fchdir(struct bfree_fs *fs, int fd)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (ofd->vnode->type != BFREE_VNODE_DIR)
		return -ENOTDIR;
	fs->cwd = ofd->vnode;
	return 0;
}

int bfree_access(struct bfree_fs *fs, const char *path, int mode)
{
	struct bfree_vnode *vn;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	return vnode_check_access(vn, mode);
}

int bfree_faccessat(struct bfree_fs *fs, int dirfd, const char *path, int mode,
		    int flags)
{
	struct bfree_vnode *vn;

	(void)flags; /* AT_EACCESS / AT_SYMLINK_NOFOLLOW: best-effort */
	if (path_is_absolute(path))
		vn = bfree_lookup(fs, path);
	else
		vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	return vnode_check_access(vn, mode);
}

static int truncate_vnode(struct bfree_vnode *vn, off_t length)
{
	if (vn == NULL)
		return -ENOENT;
	if (vn->type != BFREE_VNODE_FILE)
		return -EINVAL;
	if (length < 0)
		return -EINVAL;
	if ((size_t)length > vn->size) {
		char *n = realloc(vn->data, (size_t)length);
		if (n == NULL && length > 0)
			return -ENOMEM;
		if (n != NULL) {
			memset(n + vn->size, 0, (size_t)length - vn->size);
			vn->data = n;
		}
	}
	vn->size = (size_t)length;
	return 0;
}

int bfree_truncate(struct bfree_fs *fs, const char *path, off_t length)
{
	struct bfree_vnode *vn;

	vn = bfree_lookup(fs, path);
	return truncate_vnode(vn, length);
}

int bfree_ftruncate(struct bfree_fs *fs, int fd, off_t length)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	return truncate_vnode(ofd->vnode, length);
}

int bfree_chmod(struct bfree_fs *fs, const char *path, unsigned mode)
{
	struct bfree_vnode *vn;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	vn->mode = (vn->mode & ~07777u) | (mode & 07777u);
	return 0;
}

int bfree_fchmod(struct bfree_fs *fs, int fd, unsigned mode)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	ofd->vnode->mode = (ofd->vnode->mode & ~07777u) | (mode & 07777u);
	return 0;
}

int bfree_fchmodat(struct bfree_fs *fs, int dirfd, const char *path,
		   unsigned mode, int flags)
{
	struct bfree_vnode *vn;

	(void)flags;
	if (path_is_absolute(path))
		vn = bfree_lookup(fs, path);
	else
		vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	vn->mode = (vn->mode & ~07777u) | (mode & 07777u);
	return 0;
}

int bfree_chown(struct bfree_fs *fs, const char *path, unsigned uid,
		unsigned gid)
{
	struct bfree_vnode *vn;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	if (uid != (unsigned)-1)
		vn->uid = uid;
	if (gid != (unsigned)-1)
		vn->gid = gid;
	return 0;
}

int bfree_fchown(struct bfree_fs *fs, int fd, unsigned uid, unsigned gid)
{
	struct bfree_ofd *ofd;

	ofd = ofd_from_fd(fs, fd);
	if (ofd == NULL)
		return -EBADF;
	if (uid != (unsigned)-1)
		ofd->vnode->uid = uid;
	if (gid != (unsigned)-1)
		ofd->vnode->gid = gid;
	return 0;
}

int bfree_fchownat(struct bfree_fs *fs, int dirfd, const char *path,
		   unsigned uid, unsigned gid, int flags)
{
	struct bfree_vnode *vn;

	(void)flags;
	if (path_is_absolute(path))
		vn = bfree_lookup(fs, path);
	else
		vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	if (uid != (unsigned)-1)
		vn->uid = uid;
	if (gid != (unsigned)-1)
		vn->gid = gid;
	return 0;
}

int bfree_utimensat(struct bfree_fs *fs, int dirfd, const char *path,
		    const uint64_t times[4], int flags)
{
	struct bfree_vnode *vn;

	(void)flags;
	if (path_is_absolute(path))
		vn = bfree_lookup(fs, path);
	else
		vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	if (times != NULL) {
		vn->mtime_sec = times[2];
		vn->mtime_nsec = times[3];
	}
	return 0;
}

int bfree_mknodat(struct bfree_fs *fs, int dirfd, const char *path,
		  unsigned mode, unsigned dev)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;
	int rc;

	(void)dev;
	if (path_is_absolute(path)) {
		parent = resolve_parent(fs, path, leaf, sizeof(leaf));
		if (parent == NULL)
			return -ENOENT;
	} else {
		rc = resolve_parent_at(fs, dirfd, path, &parent, leaf,
				       sizeof(leaf));
		if (rc < 0)
			return rc;
	}
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;
	child = vnode_alloc(leaf, BFREE_VNODE_FILE, parent);
	if (child == NULL)
		return -ENOMEM;
	child->mode = mode ? mode : 0100644u;
	return vnode_add_child(parent, child);
}

int bfree_symlink(struct bfree_fs *fs, const char *target, const char *linkpath)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;

	if (target == NULL || linkpath == NULL)
		return -EFAULT;
	parent = resolve_parent(fs, linkpath, leaf, sizeof(leaf));
	if (parent == NULL || parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;
	child = vnode_alloc(leaf, BFREE_VNODE_LNK, parent);
	if (child == NULL)
		return -ENOMEM;
	child->data = malloc(strlen(target) + 1);
	if (child->data == NULL) {
		free(child);
		return -ENOMEM;
	}
	memcpy(child->data, target, strlen(target) + 1);
	child->size = strlen(target);
	return vnode_add_child(parent, child);
}

int bfree_symlinkat(struct bfree_fs *fs, const char *target, int newdirfd,
		    const char *linkpath)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *parent;
	struct bfree_vnode *child;
	int rc;

	if (path_is_absolute(linkpath))
		return bfree_symlink(fs, target, linkpath);
	rc = resolve_parent_at(fs, newdirfd, linkpath, &parent, leaf,
			       sizeof(leaf));
	if (rc < 0)
		return rc;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;
	child = vnode_alloc(leaf, BFREE_VNODE_LNK, parent);
	if (child == NULL)
		return -ENOMEM;
	child->data = malloc(strlen(target) + 1);
	if (child->data == NULL) {
		free(child);
		return -ENOMEM;
	}
	memcpy(child->data, target, strlen(target) + 1);
	child->size = strlen(target);
	return vnode_add_child(parent, child);
}

ssize_t bfree_readlink(struct bfree_fs *fs, const char *path, char *buf,
		       size_t bufsiz)
{
	struct bfree_vnode *vn;
	size_t n;

	vn = bfree_lookup(fs, path);
	if (vn == NULL)
		return -ENOENT;
	if (vn->type != BFREE_VNODE_LNK || vn->data == NULL)
		return -EINVAL;
	n = vn->size;
	if (n > bufsiz)
		n = bufsiz;
	memcpy(buf, vn->data, n);
	return (ssize_t)n;
}

ssize_t bfree_readlinkat(struct bfree_fs *fs, int dirfd, const char *path,
			 char *buf, size_t bufsiz)
{
	struct bfree_vnode *vn;
	size_t n;

	if (path_is_absolute(path))
		return bfree_readlink(fs, path, buf, bufsiz);
	vn = lookup_at(fs, dirfd, path);
	if (vn == NULL)
		return -ENOENT;
	if (vn->type != BFREE_VNODE_LNK || vn->data == NULL)
		return -EINVAL;
	n = vn->size;
	if (n > bufsiz)
		n = bufsiz;
	memcpy(buf, vn->data, n);
	return (ssize_t)n;
}

int bfree_link(struct bfree_fs *fs, const char *oldpath, const char *newpath)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *src;
	struct bfree_vnode *parent;

	src = bfree_lookup(fs, oldpath);
	if (src == NULL)
		return -ENOENT;
	if (src->type == BFREE_VNODE_DIR)
		return -EPERM;
	parent = resolve_parent(fs, newpath, leaf, sizeof(leaf));
	if (parent == NULL || parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;
	src->nlink++;
	return vnode_add_child(parent, src);
}

int bfree_linkat(struct bfree_fs *fs, int olddirfd, const char *oldpath,
		 int newdirfd, const char *newpath, int flags)
{
	char leaf[BFREE_MAX_NAME];
	struct bfree_vnode *src;
	struct bfree_vnode *parent;
	int rc;

	(void)flags;
	if (path_is_absolute(oldpath))
		src = bfree_lookup(fs, oldpath);
	else
		src = lookup_at(fs, olddirfd, oldpath);
	if (src == NULL)
		return -ENOENT;
	if (src->type == BFREE_VNODE_DIR)
		return -EPERM;
	if (path_is_absolute(newpath)) {
		parent = resolve_parent(fs, newpath, leaf, sizeof(leaf));
		if (parent == NULL)
			return -ENOENT;
	} else {
		rc = resolve_parent_at(fs, newdirfd, newpath, &parent, leaf,
				       sizeof(leaf));
		if (rc < 0)
			return rc;
	}
	if (vnode_find_child(parent, leaf) != NULL)
		return -EEXIST;
	src->nlink++;
	return vnode_add_child(parent, src);
}

int bfree_rename(struct bfree_fs *fs, const char *oldpath, const char *newpath)
{
	char old_leaf[BFREE_MAX_NAME];
	char new_leaf[BFREE_MAX_NAME];
	struct bfree_vnode *old_parent;
	struct bfree_vnode *new_parent;
	struct bfree_vnode *child;
	struct bfree_vnode *exist;

	old_parent = resolve_parent(fs, oldpath, old_leaf, sizeof(old_leaf));
	if (old_parent == NULL)
		return -ENOENT;
	child = vnode_find_child(old_parent, old_leaf);
	if (child == NULL)
		return -ENOENT;
	new_parent = resolve_parent(fs, newpath, new_leaf, sizeof(new_leaf));
	if (new_parent == NULL || new_parent->type != BFREE_VNODE_DIR)
		return -ENOENT;
	exist = vnode_find_child(new_parent, new_leaf);
	if (exist != NULL) {
		if (exist->type == BFREE_VNODE_DIR)
			return -EEXIST;
		vnode_unlink(fs, new_parent, exist);
	}
	if (vnode_remove_child(old_parent, child) < 0)
		return -ENOENT;
	snprintf(child->name, sizeof(child->name), "%s", new_leaf);
	return vnode_add_child(new_parent, child);
}

int bfree_renameat(struct bfree_fs *fs, int olddirfd, const char *oldpath,
		   int newdirfd, const char *newpath)
{
	char abs_old[BFREE_MAX_PATH];
	char abs_new[BFREE_MAX_PATH];

	/* Absolute paths: fall through to rename. Relative: build via cwd-less
	 * helpers by reusing rename when both absolute, else resolve. */
	if (path_is_absolute(oldpath) && path_is_absolute(newpath))
		return bfree_rename(fs, oldpath, newpath);

	{
		char old_leaf[BFREE_MAX_NAME];
		char new_leaf[BFREE_MAX_NAME];
		struct bfree_vnode *old_parent;
		struct bfree_vnode *new_parent;
		struct bfree_vnode *child;
		struct bfree_vnode *exist;
		int rc;

		if (path_is_absolute(oldpath)) {
			old_parent = resolve_parent(fs, oldpath, old_leaf,
						    sizeof(old_leaf));
			if (old_parent == NULL)
				return -ENOENT;
		} else {
			rc = resolve_parent_at(fs, olddirfd, oldpath,
					       &old_parent, old_leaf,
					       sizeof(old_leaf));
			if (rc < 0)
				return rc;
		}
		child = vnode_find_child(old_parent, old_leaf);
		if (child == NULL)
			return -ENOENT;
		if (path_is_absolute(newpath)) {
			new_parent = resolve_parent(fs, newpath, new_leaf,
						    sizeof(new_leaf));
			if (new_parent == NULL)
				return -ENOENT;
		} else {
			rc = resolve_parent_at(fs, newdirfd, newpath,
					       &new_parent, new_leaf,
					       sizeof(new_leaf));
			if (rc < 0)
				return rc;
		}
		exist = vnode_find_child(new_parent, new_leaf);
		if (exist != NULL && exist != child) {
			if (exist->type == BFREE_VNODE_DIR)
				return -EEXIST;
			vnode_unlink(fs, new_parent, exist);
		}
		if (vnode_remove_child(old_parent, child) < 0)
			return -ENOENT;
		snprintf(child->name, sizeof(child->name), "%s", new_leaf);
		(void)abs_old;
		(void)abs_new;
		return vnode_add_child(new_parent, child);
	}
}

int bfree_fsync_path(struct bfree_fs *fs, int fd)
{
	if (ofd_from_fd(fs, fd) == NULL)
		return -EBADF;
	if (fs->mounted)
		return bfree_fs_sync(fs);
	return 0;
}

int bfree_flock(struct bfree_fs *fs, int fd, int op)
{
	(void)op;
	if (ofd_from_fd(fs, fd) == NULL)
		return -EBADF;
	return 0;
}
