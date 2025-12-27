#ifndef GUI_COMMON_H
#define GUI_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif


// 入力イベント種別
#define GUI_EVENT_NONE   0
#define GUI_EVENT_MOUSE  1
#define GUI_EVENT_TOUCH  2
#define GUI_EVENT_KEY    3
#define GUI_EVENT_IME    4

// IME・多言語入力用
#define GUI_IME_COMPOSITION 1
#define GUI_IME_COMMIT      2

// DPIスケーリング
#define GUI_DPI_BASE 96

// テーマ切替・色覚対応
#define GUI_THEME_LIGHT  1
#define GUI_THEME_DARK   2

void gui_set_theme(int theme);
int  gui_get_theme(void);
struct gui_ime_event {
    int ime_type; // COMPOSITION/COMMIT
    char text[128];
};

struct gui_input_event {
    int type;      // GUI_EVENT_*
    int x, y;      // 座標（タッチ/マウス）
    int button;    // マウスボタン/タッチID
    int keycode;   // キーコード
    int pressed;   // 1:押下, 0:離す
    int dpi;       // DPI値（高DPI対応用）
    struct gui_ime_event ime; // IMEイベント
};

// DPIスケーリングAPI（雛形）
void gui_set_dpi(int dpi);
int  gui_get_dpi(void);

void gui_init(void);
void gui_draw_pixel(int x, int y, unsigned int color);
void gui_draw_rect(int x, int y, int w, int h, unsigned int color);
void gui_draw_text(int x, int y, const char *text, unsigned int color);
void gui_show(void);
int  gui_get_input(struct gui_input_event *event);

#ifdef __cplusplus
}
#endif

#endif // GUI_COMMON_H
