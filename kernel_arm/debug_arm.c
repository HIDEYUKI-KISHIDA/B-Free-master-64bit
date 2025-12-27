// kernel_arm/debug_arm.c
// ARM デバッグ・ロギング機能雛形
// 2025/12/27 新規作成

#include "../include_arm/debug_arm.h"
#include <stdio.h>
#include <stdarg.h>

// デバッグ出力
void debug_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// エラーログ出力
void error_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// アサート
void debug_assert(int cond, const char *msg) {
    if (!cond) {
        error_log("ASSERT FAIL: %s\n", msg);
        // TODO: 必要に応じてシステム停止やリブート
    }
}
