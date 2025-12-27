// ipc64.c - 64ビット用 IPC雛形（ダミー実装）
#include "../include64/types.h"

// パイプ（ダミー）
int pipe64(int fds[2]) {
    fds[0] = 0; fds[1] = 1;
    return 0;
}

// メッセージ送信（ダミー）
int msg_send64(int qid, const void *msg, int size) {
    return 0;
}

// メッセージ受信（ダミー）
int msg_recv64(int qid, void *msg, int size) {
    return 0;
}

// セマフォ初期化（ダミー）
int sem_init64(int *sem, int value) {
    *sem = value;
    return 0;
}

// セマフォwait（ダミー）
int sem_wait64(int *sem) {
    if (*sem > 0) (*sem)--;
    return 0;
}

// セマフォpost（ダミー）
int sem_post64(int *sem) {
    (*sem)++;
    return 0;
}

// イベントフラグ（ダミー）
int eventflag_set64(int *flag, int mask) {
    *flag |= mask;
    return 0;
}
int eventflag_clr64(int *flag, int mask) {
    *flag &= ~mask;
    return 0;
}
int eventflag_wait64(int *flag, int mask) {
    // ダミー: 常に成功
    return 0;
}
