// kernel_arm/syscall_arm.c
// ARM/64ビット システムコール・API枠組み雛形
// 2025/12/27 新規作成

#include "../include_arm/types_arm.h"
#include <stdint.h>
#include <stddef.h>

#define SYS_MAX 16

typedef int (*syscall_func_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

static syscall_func_t syscall_table[SYS_MAX];

// システムコール登録
void syscall_register(int num, syscall_func_t func) {
    if (num >= 0 && num < SYS_MAX) syscall_table[num] = func;
}

// システムコール呼び出し（カーネル内部用）
int syscall_dispatch(int num, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    if (num >= 0 && num < SYS_MAX && syscall_table[num])
        return syscall_table[num](a, b, c, d);
    return -1;
}

// ユーザーランドからのシステムコール呼び出し（雛形）
int syscall(int num, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    // TODO: SVC命令/割り込みでカーネルにトラップ（ユーザー/カーネル空間切替）
    // TODO: ARM: SVC #imm, x86: int 0x80 など
    // TODO: 引数管理・レジスタ保存/復元
    return syscall_dispatch(num, a, b, c, d); // 仮: 直接呼び出し
}

// サンプル: getpidシステムコール
int sys_getpid(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    // TODO: 現在のプロセスID取得（プロセステーブル参照）
    return 1;
}

// サンプル: writeシステムコール
int sys_write(uintptr_t fd, uintptr_t buf, uintptr_t size, uintptr_t unused) {
    // TODO: fd, buf, sizeを使ってファイル/デバイスに書き込み
    // 例: UART出力やVFS経由ファイル書き込み
    return (int)size;
}

void syscall_init(void) {
    for (int i = 0; i < SYS_MAX; ++i) syscall_table[i] = 0;
    syscall_register(0, sys_getpid);
    syscall_register(1, sys_write);
    // TODO: 他のシステムコールも順次登録
}
