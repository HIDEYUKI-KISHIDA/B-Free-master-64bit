/*
 * B-Free x86_64 guest — open-file descriptions and synthetic vnode FS.
 *
 * M1: directory enumeration cursor (dirent_index) lives on each OFD, not on
 * the vnode, so two independent open() calls on the same directory advance
 * separate positions (Linux/POSIX semantics).  dup() shares the OFD and thus
 * the dirent cursor.
 */
#ifndef BFREE_FS_OFD_H
#define BFREE_FS_OFD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BFREE_MAX_NAME   64
#define BFREE_MAX_CHILD  32
#define BFREE_MAX_OFD    64
#define BFREE_MAX_FD     64

typedef enum {
	BFREE_VNODE_FILE = 0,
	BFREE_VNODE_DIR  = 1,
} bfree_vtype_t;

struct bfree_vnode {
	char            name[BFREE_MAX_NAME];
	bfree_vtype_t   type;
	struct bfree_vnode *parent;
	struct bfree_vnode *children[BFREE_MAX_CHILD];
	size_t          child_count;
	/* Regular-file payload (synthetic in-memory FS). */
	char           *data;
	size_t          size;
};

struct bfree_ofd {
	struct bfree_vnode *vnode;
	off_t           offset;       /* regular-file byte offset */
	size_t          dirent_index; /* directory enumeration cursor (per OFD) */
	int             flags;
	unsigned        refcount;
};

struct bfree_fs {
	struct bfree_vnode root;
	struct bfree_ofd ofd_table[BFREE_MAX_OFD];
	int            fd_ofd[BFREE_MAX_FD]; /* fd -> ofd index, -1 if free */
	int            next_ofd;
};

void bfree_fs_init(struct bfree_fs *fs);

int  bfree_open(struct bfree_fs *fs, const char *path, int flags, int mode);
int  bfree_dup(struct bfree_fs *fs, int fd);
int  bfree_close(struct bfree_fs *fs, int fd);

ssize_t bfree_read(struct bfree_fs *fs, int fd, void *buf, size_t count);
ssize_t bfree_write(struct bfree_fs *fs, int fd, const void *buf, size_t count);
off_t   bfree_lseek(struct bfree_fs *fs, int fd, off_t offset, int whence);

int bfree_mkdir(struct bfree_fs *fs, const char *path, int mode);
int bfree_rmdir(struct bfree_fs *fs, const char *path);

/*
 * Linux getdents64 layout (minimal subset for guest tests).
 * d_reclen is set by the implementation.
 */
struct bfree_linux_dirent64 {
	uint64_t d_ino;
	int64_t  d_off;
	unsigned short d_reclen;
	unsigned char  d_type;
	char           d_name[];
};

ssize_t bfree_getdents64(struct bfree_fs *fs, int fd, void *buf, size_t count);

/* Test helpers */
struct bfree_ofd *bfree_ofd_for_fd(struct bfree_fs *fs, int fd);
struct bfree_vnode *bfree_lookup(struct bfree_fs *fs, const char *path);

#endif /* BFREE_FS_OFD_H */
