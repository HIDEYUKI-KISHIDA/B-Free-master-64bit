/*
 * B-Free x86_64 guest — open-file descriptions and synthetic vnode FS.
 *
 * M1 milestones:
 * - dirent_index per OFD (not shared on vnode)
 * - unlink-while-open via vnode refcount + deferred free
 * - openat/unlinkat/mkdirat dirfd-relative resolution
 * - persistent block FS via blk_vol + blk_persist
 */
#ifndef BFREE_FS_OFD_H
#define BFREE_FS_OFD_H

#include "blk_vol.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BFREE_MAX_NAME   64
#define BFREE_MAX_PATH   256
#define BFREE_MAX_CHILD  32
#define BFREE_MAX_OFD    64
#define BFREE_MAX_FD     64

#define BFREE_AT_FDCWD   (-100)
#define BFREE_FD_CLOEXEC 1

typedef enum {
	BFREE_VNODE_FILE = 0,
	BFREE_VNODE_DIR  = 1,
	BFREE_VNODE_DEV  = 2,
	BFREE_VNODE_LNK  = 3,
} bfree_vtype_t;

#define BFREE_DEV_NULL     1
#define BFREE_DEV_ZERO     2
#define BFREE_DEV_CONSOLE  3

struct bfree_vnode {
	char            name[BFREE_MAX_NAME];
	bfree_vtype_t   type;
	struct bfree_vnode *parent;
	struct bfree_vnode *children[BFREE_MAX_CHILD];
	size_t          child_count;
	unsigned        nref;
	unsigned        nlink;
	int             unlinked;
	uint32_t        ino;
	uint32_t        dev_id;
	uint32_t        mode;
	uint32_t        uid;
	uint32_t        gid;
	uint32_t        data_blks[BFREE_BLK_MAX_FILE_BLKS];
	uint32_t        data_blk_count;
	char           *data;   /* ephemeral / symlink target */
	size_t          size;
	uint64_t        mtime_sec;
	uint64_t        mtime_nsec;
};

struct bfree_ofd {
	struct bfree_vnode *vnode;
	off_t           offset;
	size_t          dirent_index;
	int             flags;
	unsigned        refcount;
};

struct bfree_fs {
	struct bfree_vnode root;
	struct bfree_vnode *cwd;
	struct bfree_ofd ofd_table[BFREE_MAX_OFD];
	int            fd_ofd_store[BFREE_MAX_FD];
	int            fd_flags_store[BFREE_MAX_FD];
	/* Active FD table (Cat4: points at per-process tables when bound). */
	int           *fd_ofd;
	int           *fd_flags;
	int            next_ofd;
	unsigned       umask;
	struct bfree_blk_vol *vol;
	bfree_blk_super_t super;
	int            mounted;
	struct bfree_vnode *vnodes[BFREE_BLK_MAX_INODES];
	size_t         vnode_count;
};

void bfree_fs_init(struct bfree_fs *fs);

int  bfree_fs_mount(struct bfree_fs *fs, const char *path);
int  bfree_fs_sync(struct bfree_fs *fs);
void bfree_fs_umount(struct bfree_fs *fs);

int  bfree_open(struct bfree_fs *fs, const char *path, int flags, int mode);
int  bfree_openat(struct bfree_fs *fs, int dirfd, const char *path,
		  int flags, int mode);
int  bfree_dup(struct bfree_fs *fs, int fd);
int  bfree_dup2(struct bfree_fs *fs, int oldfd, int newfd);
int  bfree_fcntl(struct bfree_fs *fs, int fd, int cmd, long arg);
int  bfree_close(struct bfree_fs *fs, int fd);

int  bfree_chdir(struct bfree_fs *fs, const char *path);
int  bfree_getcwd(struct bfree_fs *fs, char *buf, size_t size);

ssize_t bfree_read(struct bfree_fs *fs, int fd, void *buf, size_t count);
ssize_t bfree_write(struct bfree_fs *fs, int fd, const void *buf, size_t count);
off_t   bfree_lseek(struct bfree_fs *fs, int fd, off_t offset, int whence);

int bfree_create(struct bfree_fs *fs, const char *path, int mode);
int bfree_mkdir(struct bfree_fs *fs, const char *path, int mode);
int bfree_mkdirat(struct bfree_fs *fs, int dirfd, const char *path, int mode);
int bfree_unlink(struct bfree_fs *fs, const char *path);
int bfree_unlinkat(struct bfree_fs *fs, int dirfd, const char *path, int flags);
int bfree_rmdir(struct bfree_fs *fs, const char *path);
int bfree_rename(struct bfree_fs *fs, const char *oldpath, const char *newpath);
int bfree_renameat(struct bfree_fs *fs, int olddirfd, const char *oldpath,
		   int newdirfd, const char *newpath);
int bfree_link(struct bfree_fs *fs, const char *oldpath, const char *newpath);
int bfree_linkat(struct bfree_fs *fs, int olddirfd, const char *oldpath,
		 int newdirfd, const char *newpath, int flags);
int bfree_symlink(struct bfree_fs *fs, const char *target, const char *linkpath);
int bfree_symlinkat(struct bfree_fs *fs, const char *target, int newdirfd,
		    const char *linkpath);
ssize_t bfree_readlink(struct bfree_fs *fs, const char *path, char *buf,
		       size_t bufsiz);
ssize_t bfree_readlinkat(struct bfree_fs *fs, int dirfd, const char *path,
			 char *buf, size_t bufsiz);
int bfree_access(struct bfree_fs *fs, const char *path, int mode);
int bfree_faccessat(struct bfree_fs *fs, int dirfd, const char *path, int mode,
		    int flags);
int bfree_truncate(struct bfree_fs *fs, const char *path, off_t length);
int bfree_ftruncate(struct bfree_fs *fs, int fd, off_t length);
int bfree_fchdir(struct bfree_fs *fs, int fd);
int bfree_chmod(struct bfree_fs *fs, const char *path, unsigned mode);
int bfree_fchmod(struct bfree_fs *fs, int fd, unsigned mode);
int bfree_fchmodat(struct bfree_fs *fs, int dirfd, const char *path,
		   unsigned mode, int flags);
int bfree_chown(struct bfree_fs *fs, const char *path, unsigned uid,
		unsigned gid);
int bfree_fchown(struct bfree_fs *fs, int fd, unsigned uid, unsigned gid);
int bfree_fchownat(struct bfree_fs *fs, int dirfd, const char *path,
		   unsigned uid, unsigned gid, int flags);
int bfree_mknodat(struct bfree_fs *fs, int dirfd, const char *path, unsigned mode,
		  unsigned dev);
int bfree_utimensat(struct bfree_fs *fs, int dirfd, const char *path,
		    const uint64_t times[4], int flags);
unsigned bfree_umask(struct bfree_fs *fs, unsigned mask);
int bfree_fsync_path(struct bfree_fs *fs, int fd);
int bfree_flock(struct bfree_fs *fs, int fd, int op);
void bfree_fs_bind_fd_table(struct bfree_fs *fs, int *fd_ofd, int *fd_flags);
void bfree_fs_init_proc_fds(int *fd_ofd, int *fd_flags);
int bfree_fs_fork_fds(struct bfree_fs *fs, int *dst_ofd, int *dst_flags,
		      const int *src_ofd, const int *src_flags);

struct bfree_linux_dirent64 {
	uint64_t d_ino;
	int64_t  d_off;
	unsigned short d_reclen;
	unsigned char  d_type;
	char           d_name[];
};

/* Linux x86_64 stat layout (144 bytes). */
struct bfree_linux_stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t __pad0;
	uint64_t st_rdev;
	int64_t  st_size;
	int64_t  st_blksize;
	int64_t  st_blocks;
	uint64_t st_atime_sec;
	uint64_t st_atime_nsec;
	uint64_t st_mtime_sec;
	uint64_t st_mtime_nsec;
	uint64_t st_ctime_sec;
	uint64_t st_ctime_nsec;
	int64_t  __unused[3];
};

ssize_t bfree_getdents64(struct bfree_fs *fs, int fd, void *buf, size_t count);

int bfree_stat(struct bfree_fs *fs, const char *path,
	       struct bfree_linux_stat *st);
int bfree_fstat(struct bfree_fs *fs, int fd, struct bfree_linux_stat *st);
int bfree_fstatat(struct bfree_fs *fs, int dirfd, const char *path,
		  struct bfree_linux_stat *st, int flags);
void bfree_vnode_fill_stat(const struct bfree_vnode *vn,
			   struct bfree_linux_stat *st);

struct bfree_ofd *bfree_ofd_for_fd(struct bfree_fs *fs, int fd);
struct bfree_vnode *bfree_lookup(struct bfree_fs *fs, const char *path);
int bfree_vnode_is_alive(struct bfree_vnode *vn);

#endif /* BFREE_FS_OFD_H */
