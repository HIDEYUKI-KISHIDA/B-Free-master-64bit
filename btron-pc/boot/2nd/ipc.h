#ifndef __IPC_H__
#define __IPC_H__

#include "types.h"
#include "process.h"

/* メッセージの最大サイズ */
#define MSG_SIZE        256
#define MSG_QUEUE_SIZE  64

/* メッセージタイプ */
#define MSG_TYPE_DATA       0
#define MSG_TYPE_REQUEST    1
#define MSG_TYPE_RESPONSE   2
#define MSG_TYPE_EVENT      3

/* IPC操作のタイムアウト */
#define IPC_TIMEOUT_INFINITE  -1
#define IPC_TIMEOUT_NOW       0

/* メッセージ構造体 */
struct message {
    int msg_type;               /* メッセージタイプ */
    int sender_pid;             /* 送信者PID */
    int receiver_pid;           /* 受信者PID */
    int data_len;               /* データ長 */
    UWORD8 data[MSG_SIZE];      /* メッセージデータ */
    int msg_id;                 /* メッセージID */
    UWORD64 timestamp;          /* タイムスタンプ */
};

/* メッセージキュー */
struct msg_queue {
    struct message messages[MSG_QUEUE_SIZE];
    int head;                   /* キューヘッド */
    int tail;                   /* キューテール */
    int count;                  /* キュー内のメッセージ数 */
};

/* プロセスごとのIPC情報 */
struct ipc_info {
    struct msg_queue recv_queue;    /* 受信メッセージキュー */
    int waiting_for_msg;            /* メッセージ受信待機フラグ */
    int waiting_sender_pid;         /* 待機中の送信者PID */
};

/* グローバルIPC関数 */
int ipc_init(void);
int send_message(int to_pid, struct message *msg, int timeout);
int recv_message(int from_pid, struct message *msg, int timeout);
int peek_message(int from_pid);
int get_pending_messages(int pid);

/* キュー操作 */
int msg_queue_init(struct msg_queue *queue);
int msg_queue_put(struct msg_queue *queue, struct message *msg);
int msg_queue_get(struct msg_queue *queue, struct message *msg);
int msg_queue_is_empty(struct msg_queue *queue);
int msg_queue_is_full(struct msg_queue *queue);

/* デバッグ */
void ipc_debug(int pid);

#endif  /* __IPC_H__ */
