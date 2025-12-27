// user64.c - 64ビット用 ユーザー・権限管理雛形（ダミー実装）
#include "../include64/types.h"

#define MAX_USER64 4
static const char *user_names[MAX_USER64] = {"root", "user", "guest", "test"};
static int user_uids[MAX_USER64] = {0, 1000, 2000, 3000};
static int current_uid = 0;

int login64(const char *name) {
    for (int i = 0; i < MAX_USER64; i++) {
        if (!strcmp(user_names[i], name)) {
            current_uid = user_uids[i];
            return 0;
        }
    }
    return -1;
}

int logout64(void) {
    current_uid = 0;
    return 0;
}

int getuid64(void) { return current_uid; }
int setuid64(int uid) { current_uid = uid; return 0; }

// パーミッション雰囲気
int check_perm64(int uid, int perm) {
    // 本来はファイル/リソースごとに判定
    if (uid == 0) return 1; // rootは常にOK
    return (perm & 0x4) ? 1 : 0;
}
