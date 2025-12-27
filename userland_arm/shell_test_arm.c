// userland_arm/shell_test_arm.c
// シェル・コマンドインタプリタ動作テスト用サンプル
// 2025/12/27 新規作成

#include <stdio.h>

extern void shell_main(void);

int main(void) {
    printf("シェルテスト開始\n");
    shell_main();
    printf("シェルテスト終了\n");
    return 0;
}
