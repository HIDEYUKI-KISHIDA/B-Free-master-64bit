// include_arm/debug_arm.h
// ARM/64ビット・btron-pc共通デバッグAPI宣言集約
// 2025/12/27 新規作成

#ifndef DEBUG_ARM_H
#define DEBUG_ARM_H

#include <stdio.h>


// ログレベル定義
typedef enum {
    DBG_LEVEL_ERROR = 0,
    DBG_LEVEL_WARN,
    DBG_LEVEL_INFO,
    DBG_LEVEL_DEBUG,
    DBG_LEVEL_TRACE
} debug_level_t;

// ログ出力先
typedef enum {
    DBG_OUT_STDERR = 0,
    DBG_OUT_FILE,
    DBG_OUT_REMOTE
} debug_output_t;

// ログ設定API
void debug_set_level(debug_level_t level);
void debug_set_output(debug_output_t out, const char *param); // param: ファイル名やIP等

// ログ出力API
void debug_log_level(debug_level_t level, const char *fmt, ...);
void debug_dump_hex(const void *data, size_t len);
void debug_log(const char *msg);

// リモートデバッグ用API（雛形）
int debug_remote_connect(const char *ip, int port);
void debug_remote_disconnect(void);

#endif // DEBUG_ARM_H
