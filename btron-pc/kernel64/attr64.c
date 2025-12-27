// attr64.c - 64ビット用 ファイル属性/拡張属性API雛形（ダミー実装）
#include "../include64/types.h"

struct file_attr64 {
    int mode;
    int uid;
    int gid;
    int flags;
};

int chmod64(const char *path, int mode) {
    // 本来はファイル属性変更
    return 0;
}

int chown64(const char *path, int uid, int gid) {
    // 本来は所有者変更
    return 0;
}

int lsattr64(const char *path, struct file_attr64 *attr) {
    if (!attr) return -1;
    attr->mode = 0644;
    attr->uid = 0;
    attr->gid = 0;
    attr->flags = 0;
    return 0;
}
