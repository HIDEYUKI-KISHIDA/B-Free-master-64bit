// userland_arm/fs_test_arm.c
// ファイルシステム管理動作テスト用サンプル
// 2025/12/27 新規作成

#include "../../btron-pc/include_arm/types_arm.h"
#include <stdio.h>
#include <string.h>

extern void fs_init(void);
extern int fs_open(const char *name);
extern int fs_read(int fd, void *buf, uint32_t size);
extern int fs_write(int fd, const void *buf, uint32_t size);
extern int fs_close(int fd);

int main(void) {
    printf("ファイルシステムテスト開始\n");
    fs_init();
    int fd = fs_open("test.txt");
    if (fd >= 0) {
        char buf[64] = {0};
        int n = fs_read(fd, buf, sizeof(buf)-1);
        if (n > 0) printf("読み出し: %s\n", buf);
        strcpy(buf, "Hello ARM FS!\n");
        fs_write(fd, buf, strlen(buf));
        fs_close(fd);
    } else {
        printf("ファイルオープン失敗\n");
    }
    printf("ファイルシステムテスト終了\n");
    return 0;
}
