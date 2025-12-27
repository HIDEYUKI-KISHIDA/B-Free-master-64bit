// userland_arm/debug_test_arm.c
// デバッグ・ロギング機能動作テスト用サンプル
// 2025/12/27 新規作成

#include "../include_arm/debug_arm.h"
#include <stdio.h>

extern void debug_log(const char *fmt, ...);
extern void error_log(const char *fmt, ...);
extern void debug_assert(int cond, const char *msg);

int main(void) {
    printf("デバッグ・ロギングテスト開始\n");
    debug_log("debug_log: %d + %d = %d\n", 2, 3, 2+3);
    error_log("error_log: エラー発生例\n");
    debug_assert(1 == 1, "これは失敗しないアサート");
    debug_assert(1 == 0, "これは失敗するアサート");
    printf("デバッグ・ロギングテスト終了\n");
    return 0;
}
