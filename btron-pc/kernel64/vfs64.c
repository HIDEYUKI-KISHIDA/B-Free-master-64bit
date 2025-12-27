// vfs64.c - 64ビット用 仮想ファイルシステム雛形（ダミー実装）
#include "../include64/types.h"

// ファイル種別
#define VFS_TYPE_FILE   1
#define VFS_TYPE_DIR    2
#define VFS_TYPE_DEV    3
#define VFS_TYPE_MEM    4

// VFSノード構造体
struct vfs_node64 {
    const char *name;
    int type;
    void *data;
};

// mount/umount（ダミー）
int vfs_mount64(const char *path, int type) {
    // 本来はマウントテーブル管理
    return 0;
}
int vfs_umount64(const char *path) {
    return 0;
}

// ファイル種別判定（ダミー）
int vfs_gettype64(const char *path) {
    // 本来はノード検索
    if (!path) return 0;
    if (!strcmp(path, "/dev")) return VFS_TYPE_DEV;
    if (!strcmp(path, "/mem")) return VFS_TYPE_MEM;
    if (!strcmp(path, "/")) return VFS_TYPE_DIR;
    return VFS_TYPE_FILE;
}
