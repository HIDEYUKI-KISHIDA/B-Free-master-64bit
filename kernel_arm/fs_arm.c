// kernel_arm/fs_arm.c
// ARM ファイルシステム管理雛形
// 2025/12/27 新規作成

#include "../btron-pc/include_arm/types_arm.h"
#include <stdint.h>

#define MAX_FILES 128

typedef struct {
    int fd;
    int used;
    char name[32];
    uint32_t size;
    uint32_t pos;
    // ...他に必要な情報
} file_t;

static file_t file_table[MAX_FILES];

// ファイルシステム初期化
void fs_init(void) {
    for (int i = 0; i < MAX_FILES; ++i) {
        file_table[i].fd = -1;
        file_table[i].used = 0;
        file_table[i].name[0] = '\0';
        file_table[i].size = 0;
        file_table[i].pos = 0;
        // 他フィールドも初期化（必要に応じて拡張）
    }
    // VFS/実ファイルシステム初期化呼び出し（雛形）
    // vfs_init();
}

// ファイルオープン
int fs_open(const char *name) {
    for (int i = 0; i < MAX_FILES; ++i) {
        if (file_table[i].fd == -1) {
            file_table[i].fd = i+1;
            file_table[i].used = 1;
            strncpy(file_table[i].name, name, 31);
            file_table[i].name[31] = '\0';
            file_table[i].size = 0; // 仮: 実際はVFS/inodeから取得
            file_table[i].pos = 0;
            // VFS経由でinode取得・SFS等の実ファイルシステム呼び出し（雛形）
            // vfs_open(name);
            return file_table[i].fd;
        }
    }
    return -1;
}

// ファイルリード
int fs_read(int fd, void *buf, uint32_t size) {
    for (int i = 0; i < MAX_FILES; ++i) {
        if (file_table[i].fd == fd && file_table[i].used) {
            // VFS経由でinode参照・SFS等のread呼び出し（雛形）
            // int n = vfs_read(file_table[i].name, buf, size, file_table[i].pos);
            // file_table[i].pos += n;
            // 仮: 何も読まない
            return 0;
        }
    }
    return -1;
}

// ファイルライト
int fs_write(int fd, const void *buf, uint32_t size) {
    for (int i = 0; i < MAX_FILES; ++i) {
        if (file_table[i].fd == fd && file_table[i].used) {
            // VFS経由でinode参照・SFS等のwrite呼び出し（雛形）
            // int n = vfs_write(file_table[i].name, buf, size, file_table[i].pos);
            // file_table[i].pos += n;
            // 仮: 何も書かない
            return 0;
        }
    }
    return -1;
}

// ファイルクローズ
int fs_close(int fd) {
    for (int i = 0; i < MAX_FILES; ++i) {
        if (file_table[i].fd == fd && file_table[i].used) {
            // VFS経由でクローズ処理・リソース解放（雛形）
            // vfs_close(file_table[i].name);
            file_table[i].fd = -1;
            file_table[i].used = 0;
            return 0;
        }
    }
    return -1;
}
