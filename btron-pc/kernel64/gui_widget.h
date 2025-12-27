// --- 拡張・応用API雛形 ---

// ウィンドウマネージャ
struct gui_window_info {
    int id;
    int x, y, w, h;
    int visible;
    char title[64];
};
int gui_window_create(const char *title, int x, int y, int w, int h);
void gui_window_destroy(int id);
void gui_window_show(int id, int visible);
void gui_window_move(int id, int x, int y);
void gui_window_set_title(int id, const char *title);

// アプリ間通信（メッセージパッシング）
struct gui_message {
    int src_id;
    int dst_id;
    int type;
    char data[128];
};
int gui_send_message(const struct gui_message *msg);
int gui_recv_message(struct gui_message *msg);

// アクセシビリティ
void gui_widget_set_label(struct gui_widget *widget, const char *label);
void gui_widget_set_description(struct gui_widget *widget, const char *desc);
void gui_widget_set_state(struct gui_widget *widget, int state);
#ifndef GUI_WIDGET_H
#define GUI_WIDGET_H

#include "gui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ウィジェット種別
#define GUI_WIDGET_WINDOW  1
// --- 拡張: AIアシスタント・アクセシビリティAPI ---
#define GUI_WIDGET_AI_ASSISTANT  100  // AIアシスタント用ウィジェット型

// アクセシビリティAPI
void gui_widget_set_accessible_label(struct gui_widget *widget, const char *label);
void gui_widget_set_accessible_description(struct gui_widget *widget, const char *desc);
void gui_widget_set_focus(struct gui_widget *widget, int focused);
#define GUI_WIDGET_BUTTON  2
#define GUI_WIDGET_TEXTBOX 3
#define GUI_WIDGET_LISTBOX  4
#define GUI_WIDGET_SCROLLBAR 5
#define GUI_WIDGET_CHECKBOX  6
#define GUI_WIDGET_RADIO     7
#define GUI_WIDGET_MENU      8
#define GUI_WIDGET_IMAGE     9
#define GUI_WIDGET_DIALOG    10
// 必要に応じて追加

// ウィジェット構造体
struct gui_widget {
    int id;
    int type; // GUI_WIDGET_*
    int x, y, w, h;
    int visible;
    int focused;
    void *userdata; // 拡張用
    void (*draw)(struct gui_widget *self);
    void (*on_event)(struct gui_widget *self, struct gui_input_event *event);
    struct gui_widget *parent;
    struct gui_widget *children;
    struct gui_widget *next;
};


// 管理API
void gui_widget_init(void);
int  gui_widget_add(struct gui_widget *widget, struct gui_widget *parent);
void gui_widget_draw_all(void);
void gui_widget_dispatch_event(struct gui_input_event *event);
struct gui_widget *gui_widget_find_by_id(int id);

// レイアウト管理API（雛形）
void gui_widget_layout_vertical(struct gui_widget *parent, int margin);
void gui_widget_layout_horizontal(struct gui_widget *parent, int margin);

// フォーカス移動API（雛形）
void gui_widget_focus_next(void);
void gui_widget_focus_prev(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_WIDGET_H
