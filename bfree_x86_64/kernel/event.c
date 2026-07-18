// --- TK2独自IPC・システムイベント連携API拡張 ---
#include <string.h>
typedef enum {
    EVENT_TK2_IPC = 100,
    EVENT_TK2_SYSTEM,
    EVENT_TK2_USER
} tk2_event_type_t;

void event_publish_tk2_ipc(const char* channel, uintptr_t arg) {
    // 独自IPCイベントを発行（例: チャネル名をarg1に詰める）
    kernel_event_t ev = { .type = EVENT_TK2_IPC, .arg1 = (uintptr_t)channel, .arg2 = arg };
    event_publish(ev.type, ev.arg1, ev.arg2);
}

void event_publish_tk2_system(uintptr_t code, uintptr_t arg) {
    kernel_event_t ev = { .type = EVENT_TK2_SYSTEM, .arg1 = code, .arg2 = arg };
    event_publish(ev.type, ev.arg1, ev.arg2);
}
#include <stddef.h>
#include <stdint.h>

// シンプルなカーネルイベント通知・購読API雛形

typedef enum {
    EVENT_DEV_HOTPLUG,
    EVENT_POWER_CHANGE,
    EVENT_ERROR,
    EVENT_USER_DEFINED
} event_type_t;

typedef struct {
    event_type_t type;
    uintptr_t arg1;
    uintptr_t arg2;
} kernel_event_t;

#define EVENT_QUEUE_SIZE 64
static kernel_event_t event_queue[EVENT_QUEUE_SIZE];
static int event_head = 0, event_tail = 0;

// ---------------------------------------------------------------------------
// wakeup pipe: GUI イベントループ (Qt / epoll) との連携
// poll/select で gui_wakeup_read_fd を監視し、イベント到着を通知する
// ---------------------------------------------------------------------------
#define WAKEUP_PIPE_BUF 1

// B-Free カーネル内ではユーザー空間 pipe() が使えないため、
// 1バイトのリングバッファ＋「ready フラグ」でエミュレートする。
// Qt 側 QEventDispatcherBFree は gui_event_fd_get() で fd を取得し
// read() が常に成功する "always-ready" ファイルディスクリプタとして扱う。
// ホスト Linux (WSLg/デスクトップ確認環境) では実 pipe(2) を使う。

#include <unistd.h>   // pipe(), read(), write() — ホスト確認ビルド用

static int  _wakeup_pipe[2] = { -1, -1 };  // [0]=read, [1]=write
static int  _wakeup_initialized = 0;

// 初期化: gui_server 起動時に呼ぶ
int gui_event_init(void) {
    if (_wakeup_initialized) return 0;
    if (pipe(_wakeup_pipe) != 0) return -1;
    _wakeup_initialized = 1;
    return 0;
}

// 読み出し fd を返す（Qt の notifier に登録する）
int gui_event_fd_get(void) {
    return _wakeup_pipe[0];
}

// イベントが入ったら wakeup pipe に 1 バイト書く
static void wakeup_notify(void) {
    if (_wakeup_initialized) {
        char c = 1;
        (void)write(_wakeup_pipe[1], &c, 1);
    }
}

// wakeup pipe を読み捨てる（Qt のハンドラ冒頭で呼ぶ）
void gui_event_drain(void) {
    if (!_wakeup_initialized) return;
    char buf[16];
    // O_NONBLOCK なし → 読み捨てるだけ（1バイト以上書かれていることを前提）
    (void)read(_wakeup_pipe[0], buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// イベント発行（+ wakeup 通知）
// ---------------------------------------------------------------------------
void event_publish(event_type_t type, uintptr_t arg1, uintptr_t arg2) {
    event_queue[event_tail].type = type;
    event_queue[event_tail].arg1 = arg1;
    event_queue[event_tail].arg2 = arg2;
    event_tail = (event_tail + 1) % EVENT_QUEUE_SIZE;
    // オーバーフロー時は上書き
    if (event_tail == event_head) event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
    // GUI イベントループを起こす
    wakeup_notify();
}

// イベント取得
int event_subscribe(kernel_event_t *out) {
    if (event_head == event_tail) return 0; // 空
    *out = event_queue[event_head];
    event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
    return 1;
}
