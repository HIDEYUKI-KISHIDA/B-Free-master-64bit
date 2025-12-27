#ifndef __SFS_VFS_ADAPTER_H__
#define __SFS_VFS_ADAPTER_H__

#include "types.h"
#include "vfs.h"

/* SFS を VFS に統合するためのアダプタ */

/* SFS ファイルシステム操作 */
struct filesystem_ops sfs_vfs_ops = {
    /* マウント/アンマウント */
    .mount = sfs_mount_vfs,
    .umount = sfs_umount_vfs,
    
    /* inode 操作 */
    .get_inode = sfs_get_inode_vfs,
    .put_inode = sfs_put_inode_vfs,
    .read_inode = sfs_read_inode_vfs,
    .write_inode = sfs_write_inode_vfs,
    
    /* ファイル操作 */
    .read = sfs_read_vfs,
    .write = sfs_write_vfs,
    
    /* ディレクトリ操作 */
    .lookup = sfs_lookup_vfs,
    .mkdir = sfs_mkdir_vfs,
    .rmdir = sfs_rmdir_vfs,
    .create = sfs_create_vfs,
    .unlink = sfs_unlink_vfs,
};

/* VFS アダプタ関数 */
int sfs_mount_vfs(int dev_id, void **fs_private);
int sfs_umount_vfs(void *fs_private);
struct inode *sfs_get_inode_vfs(void *fs_private, unsigned long ino);
int sfs_put_inode_vfs(struct inode *inode);
int sfs_read_inode_vfs(struct inode *inode);
int sfs_write_inode_vfs(struct inode *inode);
int sfs_read_vfs(struct inode *inode, unsigned long offset, void *buf, int size);
int sfs_write_vfs(struct inode *inode, unsigned long offset, void *buf, int size);
int sfs_lookup_vfs(struct inode *dir, const char *name, struct inode **result);
int sfs_mkdir_vfs(struct inode *dir, const char *name);
int sfs_rmdir_vfs(struct inode *dir, const char *name);
int sfs_create_vfs(struct inode *dir, const char *name);
int sfs_unlink_vfs(struct inode *dir, const char *name);

#endif  /* __SFS_VFS_ADAPTER_H__ */
