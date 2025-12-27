// userland64.c - 64ビット用 カーネル/ユーザーランド分離API雛形（本物雰囲気）
#include "../include64/types.h"

static int current_mode64 = 0; // 0:kernel, 1:user

// ユーザープログラムロード（ELF雰囲気）
int load_user_program64(const char *filename, void *entry_out) {
    // 本来はELFパーサでエントリ取得・メモリ配置
    // ここでは雰囲気のみ: entry_outにダミーアドレス
    if (entry_out) *(unsigned long*)entry_out = 0x400000;
    return 0;
}

// ユーザーモードへ切替（雰囲気）
void enter_user_mode64(void) {
    current_mode64 = 1;
    // 本来はCPUのCPL/特権レベルを変更
}

// カーネルモードへ切替（雰囲気）
void enter_kernel_mode64(void) {
    current_mode64 = 0;
    // 本来は割り込み/システムコールで昇格
}

int get_mode64(void) {
    return current_mode64;
}

// ユーザープログラム実行（雰囲気）
int exec_user_program64(const char *filename) {
    unsigned long entry = 0;
    if (load_user_program64(filename, &entry) == 0) {
        // スタック・レジスタ初期化（雰囲気）
        enter_user_mode64();
        // 本来はjmp/iretqでユーザーエントリに制御移譲
        // ここでは雰囲気のみ
        extern void printk64(const char*);
        printk64("[USERLAND] User program started\n");
        // ...
        enter_kernel_mode64();
        return 0;
    }
    return -1;
}

// システムコール経由の権限切替雰囲気
int syscall_userland64(int callno, void *arg) {
    if (current_mode64 == 1) {
        enter_kernel_mode64();
        // システムコール処理（雰囲気）
        int ret = 0; // 本来はcallnoで分岐
        enter_user_mode64();
        return ret;
    }
    return -1;
}
