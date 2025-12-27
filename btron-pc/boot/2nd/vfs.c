// --- メモリ上inode管理の簡易実装 ---
#define MAX_INODES 128
static struct inode *inode_table[MAX_INODES] = {0};

struct inode *vfs_alloc_inode(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        if (!inode_table[i]) {
            inode_table[i] = (struct inode *)malloc64(sizeof(struct inode));
            if (inode_table[i]) {
                memset(inode_table[i], 0, sizeof(struct inode));
                inode_table[i]->ino = i+1;
                return inode_table[i];
            }
        }
    }
    return NULL;
}

void vfs_free_inode(struct inode *inode) {
    if (!inode) return;
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_table[i] == inode) {
            free64(inode_table[i]);
            inode_table[i] = NULL;
            return;
        }
    }
}

void vfs_register_inode(struct inode *inode) {
    // 既に登録済みなら何もしない
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_table[i] == inode) return;
    }
    for (int i = 0; i < MAX_INODES; i++) {
        if (!inode_table[i]) {
            inode_table[i] = inode;
            return;
        }
    }
}

void vfs_unregister_inode(struct inode *inode) {
    if (!inode) return;
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_table[i] == inode) {
            inode_table[i] = NULL;
            return;
        }
    }
}

struct inode *vfs_find_inode(const char *path) {
    if (!path || !*path) return NULL;
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_table[i] && strcmp(inode_table[i]->name, path) == 0) {
            return inode_table[i];
        }
    }
    return NULL;
}

int vfs_is_dir_empty(struct inode *dir) {
    if (!dir || dir->type != INODE_TYPE_DIR) return 0;
    // サブディレクトリやファイルが存在しないか確認
    for (int i = 0; i < MAX_INODES; i++) {
        if (inode_table[i] && inode_table[i] != dir && inode_table[i]->type != 0) {
            // 親ディレクトリ名が一致するものがあれば空でないとみなす（簡易）
            // 本来は親子関係を持つべきだが、ここではパスのprefix一致で代用
            if (strncmp(inode_table[i]->name, dir->name, strlen(dir->name)) == 0 &&
                inode_table[i]->name[strlen(dir->name)] == '/' ) {
                return 0; // 空でない
            }
        }
    }
    return 1; // 空
}
#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
#include "vfs.h"
#include "lib.h"
#include "memory64.h"
#include "sfs.h"

/* マウントテーブル */
struct mount_entry mount_table[MAX_MOUNTS];
struct superblock *root_fs = NULL;

/* ファイルシステム操作テーブル */
static struct filesystem_ops *fs_ops_table[4] = {NULL};

/* VFS 初期化 */
int vfs_init(void)
{
    int i;
    
    /* マウントテーブル初期化 */
    for (i = 0; i < MAX_MOUNTS; i++) {
        mount_table[i].sb = NULL;
        mount_table[i].mount_point[0] = 0;
        mount_table[i].in_use = 0;
    }
    
    console_printf("VFS initialized\n");
    
    return 0;
}

/* ファイルシステム登録 */
int vfs_register_fs(int fs_type, struct filesystem_ops *ops)
{
    if (fs_type < 0 || fs_type > 3 || !ops) {
        return -1;
    }
    
    fs_ops_table[fs_type] = ops;
    console_printf("Filesystem registered: type=%d\n", fs_type);
    
    return 0;
}

/* マウント */
int vfs_mount(const char *mount_point, int dev_id, int fs_type)
{
    int i;
    struct superblock *sb;
    struct mount_entry *entry = NULL;
    struct filesystem_ops *ops;
    
    if (!mount_point || fs_type < 0 || fs_type > 3) {
        return -1;
    }
    
    ops = fs_ops_table[fs_type];
    if (!ops) {
        console_printf("ERROR: Filesystem type %d not registered\n", fs_type);
        return -1;
    }
    
    /* 空きマウント点を探す */
    for (i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].in_use) {
            entry = &mount_table[i];
            break;
        }
    }
    
    if (!entry) {
        console_printf("ERROR: Mount table full\n");
        return -1;
    }
    
    /* スーパーブロック作成 */
    sb = (struct superblock *)malloc64(sizeof(struct superblock));
    if (!sb) {
        return -1;
    }
    
    sb->fs_type = fs_type;
    sb->dev_id = dev_id;
    sb->ops = ops;
    sb->root = NULL;
    sb->fs_private = NULL;
    
    /* ファイルシステムマウント */
    if (ops->mount && ops->mount(dev_id, &sb->fs_private) != 0) {
        console_printf("ERROR: Failed to mount filesystem\n");
        free64(sb);
        return -1;
    }
    
    /* マウントテーブルに追加 */
    entry->sb = sb;
    entry->in_use = 1;
    int j;
    for (j = 0; mount_point[j] && j < 255; j++) {
        entry->mount_point[j] = mount_point[j];
    }
    entry->mount_point[j] = 0;
    
    /* ルートマウント */
    if (root_fs == NULL) {
        root_fs = sb;
    }
    
    console_printf("Mounted filesystem type %d at %s\n", fs_type, mount_point);
    
    return 0;
}

/* アンマウント */
int vfs_umount(const char *mount_point)
{
    int i;
    struct mount_entry *entry = NULL;
    
    if (!mount_point) {
        return -1;
    }
    
    for (i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].in_use) {
            int j = 0;
            while (mount_point[j] && mount_table[i].mount_point[j]) {
                if (mount_point[j] != mount_table[i].mount_point[j]) {
                    break;
                }
                j++;
            }
            if (!mount_point[j] && !mount_table[i].mount_point[j]) {
                entry = &mount_table[i];
                break;
            }
        }
    }
    
    if (!entry) {
        return -1;
    }
    
    /* ファイルシステムアンマウント */
    if (entry->sb->ops->umount) {
        entry->sb->ops->umount(entry->sb->fs_private);
    }
    
    free64(entry->sb);
    entry->sb = NULL;
    entry->in_use = 0;
    
    console_printf("Unmounted filesystem at %s\n", mount_point);
    
    return 0;
}

/* パス名解決 */
struct inode *vfs_lookup(const char *path)
{
    struct inode *current;
    const char *p;
    char name[256];
    int i;
    
    if (!path || path[0] != '/') {
        return NULL;
    }
    
    if (!root_fs || !root_fs->ops->get_inode) {
        return NULL;
    }
    
    /* ルート inode を取得 */
    current = root_fs->ops->get_inode(root_fs->fs_private, 0);
    if (!current) {
        return NULL;
    }
    
    p = path + 1;  /* 先頭の '/' をスキップ */
    
    while (*p) {
        /* パスコンポーネント抽出 */
        i = 0;
        while (*p && *p != '/' && i < 255) {
            name[i++] = *p++;
        }
        name[i] = 0;
        
        if (i > 0) {
            /* コンポーネント検索 */
            struct inode *next = NULL;
            
            struct filesystem_ops *cops = NULL;
            if (current) {
                if (current->fs_type >= 0 && current->fs_type <= 3)
                    cops = fs_ops_table[current->fs_type];
            }

            if (!cops || !cops->lookup) {
                if (current && root_fs && root_fs->ops && root_fs->ops->put_inode)
                    root_fs->ops->put_inode(current);
                return NULL;
            }

            if (cops->lookup(current, name, &next) != 0) {
                if (current && cops->put_inode)
                    cops->put_inode(current);
                return NULL;
            }

            if (current) {
                if (cops->put_inode)
                    cops->put_inode(current);
            }
            current = next;
        }
        
        if (*p == '/') p++;  /* '/' をスキップ */
    }
    
    return current;
}

/* ディレクトリ内で検索 */
struct inode *vfs_lookup_dir(struct inode *dir, const char *name)
{
    struct inode *result = NULL;
    
    if (!dir || !name) return NULL;

    struct filesystem_ops *ops = NULL;
    if (dir->fs_type >= 0 && dir->fs_type <= 3) ops = fs_ops_table[dir->fs_type];
    if (!ops || !ops->lookup) return NULL;

    if (ops->lookup(dir, name, &result) != 0) return NULL;
    
    return result;
}

/* ファイルを開く */
struct file *vfs_open(const char *path, int flags)
{
    struct inode *inode;
    struct file *file;
    
    inode = vfs_lookup(path);
    if (!inode) {
        console_printf("ERROR: File not found: %s\n", path);
        return NULL;
    }
    
    file = (struct file *)malloc64(sizeof(struct file));
    if (!file) {
        struct filesystem_ops *ops = NULL;
        if (inode && inode->fs_type >= 0 && inode->fs_type <= 3) ops = fs_ops_table[inode->fs_type];
        if (inode && ops && ops->put_inode) ops->put_inode(inode);
        return NULL;
    }
    
    file->inode = inode;
    file->offset = 0;
    file->flags = flags;
    file->refcount = 1;
    
    return file;
}

/* ファイルを閉じる */
int vfs_close(struct file *file)
{
    if (!file) {
        return -1;
    }
    
    file->refcount--;
    
    if (file->refcount <= 0) {
        if (file->inode) {
            struct filesystem_ops *ops = NULL;
            if (file->inode->fs_type >= 0 && file->inode->fs_type <= 3) ops = fs_ops_table[file->inode->fs_type];
            if (ops && ops->put_inode) ops->put_inode(file->inode);
        }
        free64(file);
    }
    
    return 0;
}

/* ファイルを読む */
int vfs_read(struct file *file, void *buf, int size)
{
    int read_size;
    
    if (!file || !file->inode || !buf || size <= 0) {
        return -1;
    }
    
    struct filesystem_ops *ops = NULL;
    if (file->inode->fs_type >= 0 && file->inode->fs_type <= 3) ops = fs_ops_table[file->inode->fs_type];
    if (!ops || !ops->read) return -1;

    read_size = ops->read(file->inode, file->offset, buf, size);
    
    if (read_size > 0) {
        file->offset += read_size;
    }
    
    return read_size;
}

/* ファイルに書く */
int vfs_write(struct file *file, void *buf, int size)
{
    int written;
    
    if (!file || !file->inode || !buf || size <= 0) {
        return -1;
    }
    
    struct filesystem_ops *ops = NULL;
    if (file->inode->fs_type >= 0 && file->inode->fs_type <= 3) ops = fs_ops_table[file->inode->fs_type];
    if (!ops || !ops->write) return -1;

    written = ops->write(file->inode, file->offset, buf, size);
    
    if (written > 0) {
        file->offset += written;
    }
    
    return written;
}

/* ファイル位置を変更 */
int vfs_seek(struct file *file, long offset)
{
    if (!file || !file->inode) {
        return -1;
    }
    
    if (offset < 0 || offset > (long)file->inode->size) {
        return -1;
    }
    
    file->offset = offset;
    
    return 0;
}

/* ディレクトリ作成 */

// 仮実装→雛形: 親ディレクトリの存在確認・親inode登録
int vfs_mkdir(const char *path)
{
    if (!path || !*path) return -1;
    // 既存チェック
    if (vfs_find_inode(path)) {
        console_printf("mkdir: already exists: %s\n", path);
        return -1;
    }
    // 親ディレクトリパス抽出
    char parent_path[256] = {0};
    strncpy(parent_path, path, sizeof(parent_path)-1);
    char *slash = strrchr(parent_path, '/');
    if (slash && slash != parent_path) {
        *slash = '\0';
    } else {
        strcpy(parent_path, "/");
    }
    struct inode *parent = vfs_find_inode(parent_path);
    // 親ディレクトリの存在・型・階層整合性を厳密チェック
    if (!parent) {
        console_printf("mkdir: parent not found: %s\n", parent_path);
        return -1;
    }
    if (parent->type != INODE_TYPE_DIR) {
        console_printf("mkdir: parent is not a directory: %s\n", parent_path);
        return -1;
    }
    // 階層整合性（親がルート以外の場合は階層を検証）
    if (strcmp(parent_path, "/") != 0 && parent->parent == NULL) {
        console_printf("mkdir: parent hierarchy broken: %s\n", parent_path);
        return -1;
    }
    // 新規inode作成
    struct inode *newdir = vfs_alloc_inode();
    if (!newdir) return -1;
    newdir->type = INODE_TYPE_DIR;
    strncpy(newdir->name, path, sizeof(newdir->name)-1);
    newdir->name[sizeof(newdir->name)-1] = '\0';
    newdir->parent = parent; // 階層管理用
    newdir->child_count = 0;
    memset(newdir->children, 0, sizeof(newdir->children));
    // 親inodeの子リストに追加
    if (parent->child_count < MAX_INODES) {
        parent->children[parent->child_count++] = newdir;
    }
    vfs_register_inode(newdir);
    return 0;
}

/* ディレクトリ削除 */

// 仮実装: メモリ上のディレクトリ削除（空でなければ失敗）
int vfs_rmdir(const char *path)
{
    if (!path || !*path) return -1;
    struct inode *dir = vfs_find_inode(path);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        console_printf("rmdir: not found or not a dir: %s\n", path);
        return -1;
    }
    // 子inodeが存在する場合は削除不可
    if (dir->child_count > 0) {
        console_printf("rmdir: not empty (children exist): %s\n", path);
        return -1;
    }
    // 親inodeの子リストから削除
    struct inode *parent = dir->parent;
    if (parent) {
        for (int i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == dir) {
                for (int j = i; j < parent->child_count - 1; j++) {
                    parent->children[j] = parent->children[j+1];
                }
                parent->children[parent->child_count - 1] = NULL;
                parent->child_count--;
                break;
            }
        }
    }
    vfs_unregister_inode(dir);
    vfs_free_inode(dir);
    return 0;
}

/* ファイル削除 */

// 仮実装: メモリ上のファイル削除
int vfs_unlink(const char *path)
{
    if (!path || !*path) return -1;
    struct inode *file = vfs_find_inode(path);
    if (!file) {
        console_printf("unlink: not found: %s\n", path);
        return -1;
    }
    if (file->type == INODE_TYPE_DIR) {
        console_printf("unlink: is a dir (use rmdir): %s\n", path);
        return -1;
    }
    // 親inodeの子リストから削除
    struct inode *parent = file->parent;
    if (parent) {
        for (int i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == file) {
                for (int j = i; j < parent->child_count - 1; j++) {
                    parent->children[j] = parent->children[j+1];
                }
                parent->children[parent->child_count - 1] = NULL;
                parent->child_count--;
                break;
            }
        }
    }
    vfs_unregister_inode(file);
    vfs_free_inode(file);
    return 0;
}

/* ディレクトリを読む */
int vfs_readdir(struct inode *dir, struct dirent *entry, int index)
{
    if (!dir || !entry) return -1;
    if (dir->type != INODE_TYPE_DIR) return -1;

    struct filesystem_ops *ops = NULL;
    if (dir->fs_type >= 0 && dir->fs_type <= 3) ops = fs_ops_table[dir->fs_type];
    if (!ops || !ops->read) return -1;

    /* For SFS: read raw directory entry at offset = index * sizeof(sfs_dir) */
    if (dir->fs_type == FS_TYPE_SFS) {
        unsigned long sfs_dir_size = sizeof(struct sfs_dir);
        unsigned long offset = (unsigned long)index * sfs_dir_size;
        void *buf = malloc64(sfs_dir_size);
        if (!buf) return -1;

        int r = ops->read(dir, offset, buf, (int)sfs_dir_size);
        if (r <= 0) {
            free64(buf);
            return -1;
        }

        struct sfs_dir *sd = (struct sfs_dir *)buf;
        if (sd->sfs_d_index == 0) {
            free64(buf);
            return -1; /* no entry */
        }

        entry->ino = sd->sfs_d_index;
        entry->offset = (unsigned long)index;
        entry->name_len = (unsigned short)strnlen((char *)sd->sfs_d_name, SFS_MAXNAMELEN);
        entry->file_type = 0;
        /* ensure null-terminated name */
        int copylen = (entry->name_len < (int)sizeof(entry->name)-1) ? entry->name_len : (int)sizeof(entry->name)-1;
        memset(entry->name, 0, sizeof(entry->name));
        memcpy(entry->name, sd->sfs_d_name, copylen);

        free64(buf);
        return 0;
    }

    /* Fallback: unsupported FS type */
    return -1;
}

/* ファイル情報取得 */
int vfs_stat(const char *path, struct inode **inode_p)
{
    struct inode *inode;
    
    inode = vfs_lookup(path);
    if (!inode) {
        return -1;
    }
    
    if (inode_p) {
        *inode_p = inode;
    }
    
    return 0;
}
