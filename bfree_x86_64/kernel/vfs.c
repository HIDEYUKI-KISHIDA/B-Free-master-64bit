#include <string.h>
#include <stddef.h>

// 仮想ファイルシステム（VFS）層雛形

typedef struct vfs_node {
    const char *path;
    int (*open)(void);
    int (*read)(void *buf, size_t len);
    int (*write)(const void *buf, size_t len);
    int (*ioctl)(int cmd, void *arg);
    int (*getattr)(void *buf, size_t len);
    int (*setattr)(const void *buf, size_t len);
    // ...拡張用...
} vfs_node_t;

#define VFS_MAX_NODES 256
static vfs_node_t vfs_table[VFS_MAX_NODES];
static int vfs_count = 0;

// ノード登録
int vfs_register(const char *path, int (*open)(void), int (*read)(void*,size_t), int (*write)(const void*,size_t), int (*ioctl)(int,void*), int (*getattr)(void*,size_t), int (*setattr)(const void*,size_t)) {
    if (vfs_count >= VFS_MAX_NODES) return -1;
    vfs_table[vfs_count].path = path;
    vfs_table[vfs_count].open = open;
    vfs_table[vfs_count].read = read;
    vfs_table[vfs_count].write = write;
    vfs_table[vfs_count].ioctl = ioctl;
    vfs_table[vfs_count].getattr = getattr;
    vfs_table[vfs_count].setattr = setattr;
    vfs_count++;
    return 0;
}

// VFSディスパッチ
int vfs_open(const char *path) {
    for (int i = 0; i < vfs_count; ++i) {
        if (strcmp(vfs_table[i].path, path) == 0 && vfs_table[i].open)
            return vfs_table[i].open();
    }
    return -1;
}
// ...read/write/ioctl/getattr/setattrも同様に実装...
