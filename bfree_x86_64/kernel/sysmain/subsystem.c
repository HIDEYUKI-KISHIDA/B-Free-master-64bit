#include "subsystem.h"

extern void uart_puts(const char *s);
extern void fbdev_init(void);

// --- 入力サブシステム ---
// evdev 互換の入力ドライバを初期化する。
// 実装が存在しない場合は weak シンボルでスキップする。
__attribute__((weak)) void input_subsystem_init(void) {
    uart_puts("[SUBSYSTEM] input init (weak stub)\n");
}

// --- タイマサブシステム ---
// ハードウェアタイマ (PIT/HPET) の高レベル管理レイヤを初期化する。
// knl_timer_init() はCPU初期化後に main.c 側で呼んでいるが、
// タイマイベントキューの紐付けはここで行う。
__attribute__((weak)) void timer_subsystem_init(void) {
    uart_puts("[SUBSYSTEM] timer subsystem init (weak stub)\n");
}

// --- イベントバスとの紐付け ---
// gui_event_init() で wakeup pipe を開き、
// event.c の event_publish() が wakeup_notify() を叩けるようにする。
__attribute__((weak)) void event_subsystem_init(void) {
    uart_puts("[SUBSYSTEM] event bus init (weak stub)\n");
    // ホスト確認ビルドでは gui_event_init() を呼ぶ
    // カーネルビルドでは pipe() が使えないため weak 実装のみ
}

static void gui_subsystem_init(void)
{
    uart_puts("[SUBSYSTEM] gui init\n");
    fbdev_init();
}

void subsystem_init_all(void)
{
    uart_puts("[SUBSYSTEM] init all\n");

    // 1) GUI (fbdev)
    gui_subsystem_init();

    // 2) 入力
    input_subsystem_init();

    // 3) タイマサブシステム高レベル層
    timer_subsystem_init();

    // 4) イベントバス (wakeup pipe 初期化)
    event_subsystem_init();

    uart_puts("[SUBSYSTEM] all subsystems initialized\n");
}