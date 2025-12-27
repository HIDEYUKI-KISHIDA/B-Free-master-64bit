// kernel_arm/arch/exception_arm.c
// ARM例外・システムコール処理
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>

// 例外ベクタ初期化
void exception_vector_init(void) {
    // TODO: 例外ベクタテーブルの設定
}

// 例外ハンドラ
void exception_handler(uint32_t type, uint32_t pc, uint32_t sp) {
    // TODO: 例外種別ごとの処理
}

// システムコールAPI
int syscall_dispatch(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    // TODO: システムコール番号に応じた処理分岐
    return -1;
}
