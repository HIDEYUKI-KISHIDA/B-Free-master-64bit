#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
#include "ipc.h"
#include "lib.h"
#include "memory64.h"

/* IPC情報配列（各プロセス用） */
static struct ipc_info ipc_table[MAX_PROCESS];

/* グローバルメッセージカウンタ */
static int global_msg_id = 0;

/* タイムスタンプ用カウンタ */
static UWORD64 timestamp_counter = 0;

/* IPC初期化 */
int ipc_init(void)
{
    int i;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        msg_queue_init(&ipc_table[i].recv_queue);
        ipc_table[i].waiting_for_msg = 0;
        ipc_table[i].waiting_sender_pid = -1;
    }
    
    return 0;
}

/* メッセージキュー初期化 */
int msg_queue_init(struct msg_queue *queue)
{
    if (!queue) return -1;
    
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    return 0;
}

/* キューが空か確認 */
int msg_queue_is_empty(struct msg_queue *queue)
{
    if (!queue) return 1;
    return queue->count == 0;
}

/* キューが満杯か確認 */
int msg_queue_is_full(struct msg_queue *queue)
{
    if (!queue) return 1;
    return queue->count >= MSG_QUEUE_SIZE;
}

/* メッセージをキューに追加 */
int msg_queue_put(struct msg_queue *queue, struct message *msg)
{
    if (!queue || !msg) return -1;
    
    if (msg_queue_is_full(queue)) {
        return -1;  /* キュー満杯 */
    }
    
    /* メッセージをキューのテール位置にコピー */
    char *src = (char *)msg;
    char *dst = (char *)&queue->messages[queue->tail];
    int i;
    for (i = 0; i < sizeof(struct message); i++) {
        dst[i] = src[i];
    }
    
    /* テールを次の位置に移動 */
    queue->tail = (queue->tail + 1) % MSG_QUEUE_SIZE;
    queue->count++;
    
    return 0;
}

/* キューからメッセージを取得 */
int msg_queue_get(struct msg_queue *queue, struct message *msg)
{
    if (!queue || !msg) return -1;
    
    if (msg_queue_is_empty(queue)) {
        return -1;  /* キュー空 */
    }
    
    /* メッセージをキューのヘッド位置からコピー */
    char *src = (char *)&queue->messages[queue->head];
    char *dst = (char *)msg;
    int i;
    for (i = 0; i < sizeof(struct message); i++) {
        dst[i] = src[i];
    }
    
    /* ヘッドを次の位置に移動 */
    queue->head = (queue->head + 1) % MSG_QUEUE_SIZE;
    queue->count--;
    
    return 0;
}

/* メッセージ送信 */
int send_message(int to_pid, struct message *msg, int timeout)
{
    struct process *to_proc;
    int slot = -1;
    int i;
    
    if (!msg) {
        return -1;
    }
    
    /* 受信者プロセスを確認 */
    to_proc = get_process(to_pid);
    if (!to_proc || to_proc->state == PROC_FREE || to_proc->state == PROC_DEAD) {
        console_printf("ERROR: Invalid receiver PID %d\n", to_pid);
        return -1;
    }
    
    /* プロセステーブルからスロットを探す */
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == to_pid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -1;
    }
    
    /* メッセージ情報を設定 */
    msg->sender_pid = current_pid;
    msg->receiver_pid = to_pid;
    msg->msg_id = global_msg_id++;
    msg->timestamp = timestamp_counter++;
    
    /* 受信キューに追加 */
    if (msg_queue_put(&ipc_table[slot].recv_queue, msg) != 0) {
        console_printf("ERROR: Message queue full for PID %d\n", to_pid);
        return -1;
    }
    
    console_printf("Message sent: from %d to %d (ID=%d)\n", 
                   msg->sender_pid, to_pid, msg->msg_id);
    
    return msg->msg_id;
}

/* メッセージ受信 */
int recv_message(int from_pid, struct message *msg, int timeout)
{
    int slot = -1;
    int i;
    struct msg_queue *queue;
    
    if (!msg) {
        return -1;
    }
    
    /* 現在のプロセスのIPCスロットを探す */
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == current_pid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -1;
    }
    
    queue = &ipc_table[slot].recv_queue;
    
    /* 指定された送信者からのメッセージを探す */
    if (from_pid == -1) {
        /* 任意の送信者からの受信 */
        if (!msg_queue_is_empty(queue)) {
            if (msg_queue_get(queue, msg) == 0) {
                console_printf("Message received: from %d to %d (ID=%d)\n",
                              msg->sender_pid, msg->receiver_pid, msg->msg_id);
                return msg->msg_id;
            }
        }
    } else {
        /* 指定した送信者からの受信 */
        int j;
        for (j = 0; j < queue->count; j++) {
            int msg_idx = (queue->head + j) % MSG_QUEUE_SIZE;
            if (queue->messages[msg_idx].sender_pid == from_pid) {
                /* 該当メッセージを取り出す */
                char *src = (char *)&queue->messages[msg_idx];
                char *dst = (char *)msg;
                int k;
                for (k = 0; k < sizeof(struct message); k++) {
                    dst[k] = src[k];
                }
                
                /* キューを再構成 */
                queue->count--;
                
                console_printf("Message received: from %d to %d (ID=%d)\n",
                              msg->sender_pid, msg->receiver_pid, msg->msg_id);
                return msg->msg_id;
            }
        }
    }
    
    /* メッセージがない場合 */
    if (timeout == IPC_TIMEOUT_NOW) {
        return -1;  /* ブロッキングなし */
    }
    
    /* タイムアウト付き待機が必要な場合（後で実装） */
    ipc_table[slot].waiting_for_msg = 1;
    ipc_table[slot].waiting_sender_pid = from_pid;
    
    return -2;  /* 受信待機中 */
}

/* メッセージをピークする（取り出さない） */
int peek_message(int from_pid)
{
    int slot = -1;
    int i;
    struct msg_queue *queue;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == current_pid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return 0;
    }
    
    queue = &ipc_table[slot].recv_queue;
    
    if (from_pid == -1) {
        /* 任意のメッセージをピーク */
        return !msg_queue_is_empty(queue);
    } else {
        /* 指定送信者のメッセージをピーク */
        int j;
        for (j = 0; j < queue->count; j++) {
            int msg_idx = (queue->head + j) % MSG_QUEUE_SIZE;
            if (queue->messages[msg_idx].sender_pid == from_pid) {
                return 1;
            }
        }
    }
    
    return 0;
}

/* 保留中のメッセージ数を取得 */
int get_pending_messages(int pid)
{
    int slot = -1;
    int i;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == pid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return 0;
    }
    
    return ipc_table[slot].recv_queue.count;
}

/* IPC デバッグ情報表示 */
void ipc_debug(int pid)
{
    int slot = -1;
    int i, j;
    struct msg_queue *queue;
    struct message *msg;
    
    for (i = 0; i < MAX_PROCESS; i++) {
        if (proc_table[i].pid == pid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        console_printf("ERROR: Process not found\n");
        return;
    }
    
    queue = &ipc_table[slot].recv_queue;
    
    console_printf("IPC Debug for PID %d:\n", pid);
    console_printf("  Pending messages: %d\n", queue->count);
    console_printf("  Message queue:\n");
    
    for (j = 0; j < queue->count; j++) {
        int msg_idx = (queue->head + j) % MSG_QUEUE_SIZE;
        msg = &queue->messages[msg_idx];
        console_printf("    [%d] From=%d, Type=%d, Len=%d, ID=%d\n",
                      j, msg->sender_pid, msg->msg_type, msg->data_len, msg->msg_id);
    }
}
