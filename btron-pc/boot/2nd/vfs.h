#ifndef __VFS_H__
#define __VFS_H__

#include "types.h"

/* ファイルシステムタイプ */
#define FS_TYPE_SFS     0   /* Simple File System */
#define FS_TYPE_FAT32   1
#define FS_TYPE_EXT2    2

/* inode 種類 */
#define INODE_TYPE_REG  1   /* 通常ファイル */
#define INODE_TYPE_DIR  2   /* ディレクトリ */
#define INODE_TYPE_CHR  3   /* キャラクタデバイス */
#define INODE_TYPE_BLK  4   /* ブロックデバイス */
#define INODE_TYPE_FIFO 5   /* FIFO */
#define INODE_TYPE_LNK  6   /* シンボリックリンク */

/* ファイルアクセス権限 */
#define FILE_PERM_R     0x4     /* 読み取り */
#define FILE_PERM_W     0x2     /* 書き込み */
#define FILE_PERM_X     0x1     /* 実行 */

/* ファイルアクセスモード */
#define FILE_OPEN_R     0
#define FILE_OPEN_W     1
#define FILE_OPEN_RW    2

/* inode 構造体 */
struct inode {
    int fs_type;                /* ファイルシステムタイプ */
    unsigned long ino;          /* inode 番号 */
    int type;                   /* タイプ（ファイル/ディレクトリ） */
    int permissions;            /* アクセス権限 */
    unsigned long size;         /* ファイルサイズ */
    unsigned long blocks;       /* ブロック数 */
    UWORD64 atime;              /* アクセス時刻 */
    UWORD64 mtime;              /* 更新時刻 */
    UWORD64 ctime;              /* ステータス変更時刻 */
    int link_count;             /* リンク数 */
    unsigned int uid;           /* 所有者UID */
    unsigned int gid;           /* 所有者GID */
    
    /* ファイルシステム固有データ */
    void *fs_data;              /* FS専用データへのポインタ */
    void *fs_private;           /* FS専用プライベートデータ */
    struct inode *parent;       /* 親ディレクトリへのポインタ（階層管理用） */
    struct inode *children[MAX_INODES]; /* 子inodeリスト（階層管理強化） */
    int child_count;
};

/* ファイル構造体 */
struct file {
    struct inode *inode;        /* inode ポインタ */
    unsigned long offset;       /* ファイル内位置 */
    int flags;                  /* 開きモード */
    int refcount;               /* 参照カウント */
};

/* ディレクトリエントリ */
struct dirent {
    unsigned long ino;          /* inode 番号 */
    unsigned long offset;       /* ディレクトリ内位置 */
    unsigned short name_len;    /* ファイル名長 */
    unsigned char file_type;    /* ファイルタイプ */
    char name[256];             /* ファイル名 */
};

/* ファイルシステム操作関数ポインタ */
struct filesystem_ops {
    /* マウント/アンマウント */
    int (*mount)(int dev_id, void **fs_private);
    int (*umount)(void *fs_private);
    
    /* inode 操作 */
    struct inode * (*get_inode)(void *fs_private, unsigned long ino);
    int (*put_inode)(struct inode *inode);
    int (*read_inode)(struct inode *inode);
    int (*write_inode)(struct inode *inode);
    
    /* ファイル操作 */
    int (*read)(struct inode *inode, unsigned long offset, void *buf, int size);
    int (*write)(struct inode *inode, unsigned long offset, void *buf, int size);
    
    /* ディレクトリ操作 */
    int (*lookup)(struct inode *dir, const char *name, struct inode **result);
    int (*mkdir)(struct inode *dir, const char *name);
    int (*rmdir)(struct inode *dir, const char *name);
    int (*create)(struct inode *dir, const char *name);
    int (*unlink)(struct inode *dir, const char *name);
};

/* ファイルシステム */
struct superblock {
    int fs_type;                /* ファイルシステムタイプ */
    int dev_id;                 /* デバイスID */
    struct filesystem_ops *ops; /* ファイルシステム操作 */
    void *fs_private;           /* FS専用プライベートデータ */
    struct inode *root;         /* ルート inode */
};

/* マウントテーブル */
#define MAX_MOUNTS  16

struct mount_entry {
    struct superblock *sb;      /* スーパーブロック */
    char mount_point[256];      /* マウントポイント */
    int in_use;                 /* 使用中フラグ */
};

extern struct mount_entry mount_table[MAX_MOUNTS];
extern struct superblock *root_fs;

/* VFS 初期化 */
int vfs_init(void);

/* マウント/アンマウント */
int vfs_mount(const char *mount_point, int dev_id, int fs_type);
int vfs_umount(const char *mount_point);

/* パス名解決 */
struct inode *vfs_lookup(const char *path);
struct inode *vfs_lookup_dir(struct inode *dir, const char *name);

/* ファイル操作 */
struct file *vfs_open(const char *path, int flags);
int vfs_close(struct file *file);
int vfs_read(struct file *file, void *buf, int size);
int vfs_write(struct file *file, void *buf, int size);
int vfs_seek(struct file *file, long offset);

/* ディレクトリ操作 */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_readdir(struct inode *dir, struct dirent *entry, int index);

/* ファイル情報取得 */
int vfs_stat(const char *path, struct inode **inode_p);

/* ファイルシステム登録 */
int vfs_register_fs(int fs_type, struct filesystem_ops *ops);

#endif  /* __VFS_H__ */
