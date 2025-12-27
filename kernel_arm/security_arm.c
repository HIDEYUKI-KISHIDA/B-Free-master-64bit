// kernel_arm/security_arm.c
// ARM/64ビット セキュリティ・権限管理雛形
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_USERS 16
#define MAX_GROUPS 8

typedef struct {
    int uid;
    char name[16];
    int gid;
    uint32_t perm; // 権限ビット（例: rwx）
} user_t;

typedef struct {
    int gid;
    char name[16];
} group_t;

static user_t user_table[MAX_USERS];
static group_t group_table[MAX_GROUPS];

// ユーザー/グループ初期化
void security_init(void) {
    for (int i = 0; i < MAX_USERS; ++i) {
        user_table[i].uid = -1;
        user_table[i].gid = -1;
        user_table[i].name[0] = '\0';
        user_table[i].perm = 0;
    }
    for (int i = 0; i < MAX_GROUPS; ++i) {
        group_table[i].gid = -1;
        group_table[i].name[0] = '\0';
    }
    // rootグループ自動生成
    group_table[0].gid = 0;
    strncpy(group_table[0].name, "root", 15);
    group_table[0].name[15] = '\0';
    // rootユーザー自動生成
    user_table[0].uid = 0;
    user_table[0].gid = 0;
    strncpy(user_table[0].name, "root", 15);
    user_table[0].name[15] = '\0';
    user_table[0].perm = 0x7; // rwx
}

// ユーザー追加
int user_add(const char *name, int gid) {
    for (int i = 0; i < MAX_USERS; ++i) {
        if (user_table[i].uid == -1) {
            user_table[i].uid = i+1;
            user_table[i].gid = gid;
            strncpy(user_table[i].name, name, 15);
            user_table[i].name[15] = '\0';
            user_table[i].perm = 0x7; // rwxデフォルト
            return user_table[i].uid;
        }
    }
    return -1;
}

// グループ追加
int group_add(const char *name) {
    for (int i = 0; i < MAX_GROUPS; ++i) {
        if (group_table[i].gid == -1) {
            group_table[i].gid = i+1;
            strncpy(group_table[i].name, name, 15);
            group_table[i].name[15] = '\0';
            return group_table[i].gid;
        }
    }
    return -1;
}

// アクセス制御雛形
int check_access(int uid, int resource, int perm) {
    // ユーザー権限ビット取得
    if (uid < 0 || uid >= MAX_USERS || user_table[uid].uid == -1) {
        printf("[SECURITY] invalid uid %d\n", uid);
        return 0;
    }
    uint32_t user_perm = user_table[uid].perm;
    // rwx: perm=0x1(read),0x2(write),0x4(exec)
    int allow = ((user_perm & perm) == perm);
    // セキュリティログ出力
    printf("[SECURITY] check_access: uid=%d perm=0x%x need=0x%x result=%s\n", uid, user_perm, perm, allow ? "ALLOW" : "DENY");
    return allow;
}
