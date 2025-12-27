#include <string.h>

// --- ウィンドウマネージャ簡易実装 ---
#define MAX_WINDOWS 8
static struct gui_window_info window_list[MAX_WINDOWS];
static int window_count = 0;
static int active_window = -1;

int gui_window_create(const char *title, int x, int y, int w, int h) {
    if (window_count >= MAX_WINDOWS) return -1;
    int id = window_count + 1;
    struct gui_window_info *win = &window_list[window_count++];
    win->id = id;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->visible = 1;
    strncpy(win->title, title, sizeof(win->title)-1);
    win->title[sizeof(win->title)-1] = '\0';
    if (active_window == -1) active_window = id;
    return id;
}

void gui_window_destroy(int id) {
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i].id == id) {
            for (int j = i; j < window_count-1; ++j) window_list[j] = window_list[j+1];
            window_count--;
            if (active_window == id) active_window = (window_count > 0) ? window_list[0].id : -1;
            break;
        }
    }
}

void gui_window_show(int id, int visible) {
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i].id == id) window_list[i].visible = visible;
    }
}

void gui_window_move(int id, int x, int y) {
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i].id == id) { window_list[i].x = x; window_list[i].y = y; }
    }
}

void gui_window_set_title(int id, const char *title) {
    for (int i = 0; i < window_count; ++i) {
        if (window_list[i].id == id) {
            strncpy(window_list[i].title, title, sizeof(window_list[i].title)-1);
            window_list[i].title[sizeof(window_list[i].title)-1] = '\0';
        }
    }
}

// アクティブウィンドウ切り替え
void gui_window_activate(int id) { active_window = id; }

// サンプル: ウィンドウリスト取得
int gui_window_get_list(struct gui_window_info *list, int max) {
    int n = (window_count < max) ? window_count : max;
    for (int i = 0; i < n; ++i) list[i] = window_list[i];
    return n;
}
// --- 拡張・応用API雛形 ---

// ウィンドウマネージャ（ダミー実装）
int gui_window_create(const char *title, int x, int y, int w, int h) { (void)title; (void)x; (void)y; (void)w; (void)h; return 1; }
void gui_window_destroy(int id) { (void)id; }
void gui_window_show(int id, int visible) { (void)id; (void)visible; }
void gui_window_move(int id, int x, int y) { (void)id; (void)x; (void)y; }
void gui_window_set_title(int id, const char *title) { (void)id; (void)title; }

// アプリ間通信（ダミー実装）
int gui_send_message(const struct gui_message *msg) { (void)msg; return 0; }
int gui_recv_message(struct gui_message *msg) { (void)msg; return 0; }

// アクセシビリティ（ダミー実装）
void gui_widget_set_label(struct gui_widget *widget, const char *label) { (void)widget; (void)label; }
void gui_widget_set_description(struct gui_widget *widget, const char *desc) { (void)widget; (void)desc; }
void gui_widget_set_state(struct gui_widget *widget, int state) { (void)widget; (void)state; }
static struct gui_widget *focused_widget = NULL;

// フォーカスを次のウィジェットへ
void gui_widget_focus_next(void) {
    struct gui_widget *w = focused_widget ? focused_widget->next : NULL;
    if (!w) w = root_widget;
    while (w && (!w->visible || !w->on_event)) w = w->next;
    if (w) {
        if (focused_widget) focused_widget->focused = 0;
        focused_widget = w;
        focused_widget->focused = 1;
    }
}

// フォーカスを前のウィジェットへ（簡易実装）
void gui_widget_focus_prev(void) {
    struct gui_widget *w = root_widget, *prev = NULL;
    while (w && w != focused_widget) {
        if (w->visible && w->on_event) prev = w;
        w = w->next;
    }
    if (prev) {
        if (focused_widget) focused_widget->focused = 0;
        focused_widget = prev;
        focused_widget->focused = 1;
    }
}
#include <stddef.h>

// 縦並びレイアウト（親の子ウィジェットをmargin間隔で自動配置）
void gui_widget_layout_vertical(struct gui_widget *parent, int margin) {
    if (!parent) return;
    int y = parent->y + margin;
    struct gui_widget *w = parent->children;
    while (w) {
        w->x = parent->x + margin;
        w->y = y;
        y += w->h + margin;
        w = w->next;
    }
}

// 横並びレイアウト（親の子ウィジェットをmargin間隔で自動配置）
void gui_widget_layout_horizontal(struct gui_widget *parent, int margin) {
    if (!parent) return;
    int x = parent->x + margin;
    struct gui_widget *w = parent->children;
    while (w) {
        w->x = x;
        w->y = parent->y + margin;
        x += w->w + margin;
        w = w->next;
    }
}
#include "gui_widget.h"
#include <stdlib.h>

static struct gui_widget *root_widget = NULL;
static int next_widget_id = 1;

void gui_widget_init(void) {
    root_widget = NULL;
    next_widget_id = 1;
}

int gui_widget_add(struct gui_widget *widget, struct gui_widget *parent) {
    if (!widget) return -1;
    widget->id = next_widget_id++;
    widget->parent = parent;
    widget->next = NULL;
    widget->focused = 0;
    widget->visible = 1;
    if (parent) {
        widget->next = parent->children;
        parent->children = widget;
    } else {
        widget->next = root_widget;
        root_widget = widget;
    }
    return widget->id;
}

void gui_widget_draw_all(void) {
    struct gui_widget *w = root_widget;
    while (w) {
        if (w->visible && w->draw) w->draw(w);
        w = w->next;
    }
}

void gui_widget_dispatch_event(struct gui_input_event *event) {
    struct gui_widget *w = root_widget;
    while (w) {
        if (w->visible && w->on_event) w->on_event(w, event);
        w = w->next;
    }
}

struct gui_widget *gui_widget_find_by_id(int id) {
    struct gui_widget *w = root_widget;
    while (w) {
        if (w->id == id) return w;
        w = w->next;
    }
    return NULL;
}
