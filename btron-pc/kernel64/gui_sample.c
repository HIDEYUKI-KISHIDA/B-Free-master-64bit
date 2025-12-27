#include <stdio.h>
    // --- アクセシビリティ情報付与例 ---
    gui_widget_set_label(&btn, "メインボタン");
    gui_widget_set_description(&btn, "メインウィンドウのボタンです");
    gui_widget_set_state(&btn, 0); // 0:通常
    // --- アプリ間通信サンプル ---
    struct gui_message msg = {win1_id, win2_id, 1, "Hello from Main Window!"};
    gui_send_message(&msg);
    struct gui_message recv = {0};
    if (gui_recv_message(&recv) == 0) {
        printf("Message received: %s\n", recv.data);
    }
#include "gui_widget.h"
    // --- 複数ウィンドウ生成・切り替えサンプル ---
    int win1_id = gui_window_create("Main Window", scale_by_dpi(40), scale_by_dpi(30), scale_by_dpi(180), scale_by_dpi(120));
    int win2_id = gui_window_create("Sub Window", scale_by_dpi(260), scale_by_dpi(60), scale_by_dpi(140), scale_by_dpi(100));
    gui_window_set_title(win2_id, "サブウィンドウ");
    gui_window_show(win2_id, 1);

    // サンプル: Tabキーでウィンドウ切り替え
    int current = 0;
#define DIALOG_TEXT_LEN 64

struct dialog_data {
    char text[DIALOG_TEXT_LEN];
    int visible;
};

void draw_dialog(struct gui_widget *self) {
    struct dialog_data *dd = (struct dialog_data*)self->userdata;
    if (!dd->visible) return;
    int w = 120, h = 60;
    int x = self->x, y = self->y;
    gui_draw_rect(x, y, w, h, 0xFFFFCC);
    gui_draw_rect(x, y, w, h, 0x000000); // 枠
    gui_draw_text(x + 8, y + 16, dd->text, 0x000000);
    gui_draw_rect(x + w - 28, y + h - 28, 24, 20, 0xFF8888); // OKボタン
    gui_draw_text(x + w - 22, y + h - 24, "OK", 0x000000);
}

void on_dialog_event(struct gui_widget *self, struct gui_input_event *event) {
    struct dialog_data *dd = (struct dialog_data*)self->userdata;
    if (!dd->visible) return;
    int w = 120, h = 60;
    int x = self->x, y = self->y;
    // OKボタン押下判定
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        if (event->x >= x + w - 28 && event->x < x + w - 4 && event->y >= y + h - 28 && event->y < y + h - 8) {
            dd->visible = 0;
        }
    }
    if (event->type == GUI_EVENT_KEY && event->pressed && event->keycode == 13) {
        dd->visible = 0;
    }
}
#define IMAGE_W 32
#define IMAGE_H 32

struct image_data {
    unsigned int pixels[IMAGE_W * IMAGE_H];
};

void draw_image(struct gui_widget *self) {
    struct image_data *img = (struct image_data*)self->userdata;
    for (int y = 0; y < IMAGE_H; ++y) {
        for (int x = 0; x < IMAGE_W; ++x) {
            gui_draw_pixel(self->x + x, self->y + y, img->pixels[y * IMAGE_W + x]);
        }
    }
    gui_draw_rect(self->x, self->y, IMAGE_W, IMAGE_H, 0x000000); // 枠
}
#define MENU_MAXITEMS 4
#define MENU_ITEMLEN  24

struct menu_data {
    char items[MENU_MAXITEMS][MENU_ITEMLEN];
    int count;
    int opened;
    int selected;
};

void draw_menu(struct gui_widget *self) {
    struct menu_data *md = (struct menu_data*)self->userdata;
    // メニューバー
    gui_draw_rect(self->x, self->y, self->w, 20, 0xCCCCCC);
    gui_draw_rect(self->x, self->y, self->w, 20, 0x000000); // 枠
    gui_draw_text(self->x + 4, self->y + 2, "Menu", 0x000000);
    // メニュー展開
    if (md->opened) {
        gui_draw_rect(self->x, self->y + 20, self->w, md->count * 20, 0xEEEEEE);
        gui_draw_rect(self->x, self->y + 20, self->w, md->count * 20, 0x000000);
        for (int i = 0; i < md->count; ++i) {
            int y = self->y + 20 + i * 20;
            unsigned int color = (i == md->selected) ? 0x0000FF : 0x000000;
            gui_draw_text(self->x + 4, y + 2, md->items[i], color);
        }
    }
}

void on_menu_event(struct gui_widget *self, struct gui_input_event *event) {
    struct menu_data *md = (struct menu_data*)self->userdata;
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        // メニューバークリックで開閉
        if (event->y >= self->y && event->y < self->y + 20 && event->x >= self->x && event->x < self->x + self->w) {
            md->opened = !md->opened;
        } else if (md->opened && event->y >= self->y + 20 && event->y < self->y + 20 + md->count * 20 && event->x >= self->x && event->x < self->x + self->w) {
            int idx = (event->y - self->y - 20) / 20;
            if (idx >= 0 && idx < md->count) {
                md->selected = idx;
                md->opened = 0; // 選択で閉じる
            }
        } else {
            md->opened = 0;
        }
    }
    if (event->type == GUI_EVENT_KEY && md->opened && event->pressed) {
        if (event->keycode == 38 && md->selected > 0) md->selected--; // Up
        if (event->keycode == 40 && md->selected < md->count-1) md->selected++; // Down
        if (event->keycode == 13) md->opened = 0; // Enterで閉じる
    }
}
#define RADIO_LABEL_LEN 32
#define RADIO_GROUP_MAX 3

struct radio_data {
    char label[RADIO_LABEL_LEN];
    int *group_selected;
    int my_index;
};

void draw_radio(struct gui_widget *self) {
    struct radio_data *rd = (struct radio_data*)self->userdata;
    int selected = (*(rd->group_selected) == rd->my_index);
    // 円（ラジオボタン）
    gui_draw_rect(self->x, self->y, 16, 16, 0xFFFFFF);
    gui_draw_rect(self->x, self->y, 16, 16, 0x000000); // 枠
    if (selected) {
        gui_draw_rect(self->x + 5, self->y + 5, 6, 6, 0xAA0000);
    }
    gui_draw_text(self->x + 20, self->y, rd->label, 0x000000);
}

void on_radio_event(struct gui_widget *self, struct gui_input_event *event) {
    struct radio_data *rd = (struct radio_data*)self->userdata;
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        if (event->x >= self->x && event->x < self->x + 16 && event->y >= self->y && event->y < self->y + 16) {
            *(rd->group_selected) = rd->my_index;
        }
    }
}
#define CHECKBOX_LABEL_LEN 32

struct checkbox_data {
    char label[CHECKBOX_LABEL_LEN];
    int checked;
};

void draw_checkbox(struct gui_widget *self) {
    struct checkbox_data *cb = (struct checkbox_data*)self->userdata;
    gui_draw_rect(self->x, self->y, 16, 16, 0xFFFFFF);
    gui_draw_rect(self->x, self->y, 16, 16, 0x000000); // 枠
    if (cb->checked) {
        gui_draw_rect(self->x + 3, self->y + 3, 10, 10, 0x00AA00);
    }
    gui_draw_text(self->x + 20, self->y, cb->label, 0x000000);
}

void on_checkbox_event(struct gui_widget *self, struct gui_input_event *event) {
    struct checkbox_data *cb = (struct checkbox_data*)self->userdata;
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        if (event->x >= self->x && event->x < self->x + 16 && event->y >= self->y && event->y < self->y + 16) {
            cb->checked = !cb->checked;
        }
    }
}
#define SCROLLBAR_HEIGHT 90

struct scrollbar_data {
    int min, max, pos;
};

void draw_scrollbar(struct gui_widget *self) {
    struct scrollbar_data *sb = (struct scrollbar_data*)self->userdata;
    gui_draw_rect(self->x, self->y, self->w, self->h, 0xDDDDDD); // 背景
    gui_draw_rect(self->x, self->y, self->w, self->h, 0x000000); // 枠
    // スライダー位置計算
    int range = sb->max - sb->min;
    int slider_h = 20;
    int slider_y = self->y + ((sb->pos - sb->min) * (self->h - slider_h)) / (range ? range : 1);
    gui_draw_rect(self->x + 2, slider_y, self->w - 4, slider_h, 0x8888FF);
}

void on_scrollbar_event(struct gui_widget *self, struct gui_input_event *event) {
    struct scrollbar_data *sb = (struct scrollbar_data*)self->userdata;
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        int rel_y = event->y - self->y;
        int range = sb->max - sb->min;
        int slider_h = 20;
        int pos = sb->min + (rel_y * (range ? range : 1)) / (self->h - slider_h);
        if (pos < sb->min) pos = sb->min;
        if (pos > sb->max) pos = sb->max;
        sb->pos = pos;
    }
}
#define LISTBOX_MAXITEMS 5
#define LISTBOX_ITEMLEN  24

struct listbox_data {
    char items[LISTBOX_MAXITEMS][LISTBOX_ITEMLEN];
    int count;
    int selected;
};

void draw_listbox(struct gui_widget *self) {
    struct listbox_data *lb = (struct listbox_data*)self->userdata;
    gui_draw_rect(self->x, self->y, self->w, self->h, 0xEEEEEE);
    gui_draw_rect(self->x, self->y, self->w, self->h, 0x000000); // 枠
    for (int i = 0; i < lb->count; ++i) {
        int y = self->y + 4 + i * 16;
        unsigned int color = (i == lb->selected) ? 0x0000FF : 0x000000;
        gui_draw_text(self->x + 4, y, lb->items[i], color);
    }
}

void on_listbox_event(struct gui_widget *self, struct gui_input_event *event) {
    struct listbox_data *lb = (struct listbox_data*)self->userdata;
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        int rel_y = event->y - self->y - 4;
        int idx = rel_y / 16;
        if (idx >= 0 && idx < lb->count) {
            lb->selected = idx;
        }
    }
    if (event->type == GUI_EVENT_KEY && event->pressed) {
        if (event->keycode == 38 && lb->selected > 0) lb->selected--; // Up
        if (event->keycode == 40 && lb->selected < lb->count-1) lb->selected++; // Down
    }
}
#include "gui_common.h"
#include "gui_widget.h"
#include <stdio.h>
#include <string.h>

#define TEXTBOX_MAXLEN 64

// テキストボックス用データ
struct textbox_data {
    char text[TEXTBOX_MAXLEN];
    int cursor;
};

void draw_textbox(struct gui_widget *self) {
    struct textbox_data *tb = (struct textbox_data*)self->userdata;
    gui_draw_rect(self->x, self->y, self->w, self->h, 0xFFFFFF);
    gui_draw_rect(self->x, self->y, self->w, self->h, 0x000000); // 枠
    gui_draw_text(self->x + 4, self->y + 4, tb->text, 0x000000);
    // カーソル表示（簡易）
    int tx = self->x + 4 + tb->cursor * 8;
    gui_draw_rect(tx, self->y + 4, 2, 12, 0x0000FF);
}

void on_textbox_event(struct gui_widget *self, struct gui_input_event *event) {
    struct textbox_data *tb = (struct textbox_data*)self->userdata;
    if (event->type == GUI_EVENT_KEY && event->pressed) {
        if (event->keycode == 8) { // Backspace
            if (tb->cursor > 0) {
                tb->cursor--;
                tb->text[tb->cursor] = '\0';
            }
        } else if (event->keycode >= 32 && event->keycode < 127) {
            if (tb->cursor < TEXTBOX_MAXLEN-1) {
                tb->text[tb->cursor++] = (char)event->keycode;
                tb->text[tb->cursor] = '\0';
            }
        }
    }
    // IME・多言語入力対応（雛形）
    if (event->type == GUI_EVENT_IME) {
        if (event->ime.ime_type == GUI_IME_COMMIT) {
            int len = (int)strlen(event->ime.text);
            if (tb->cursor + len < TEXTBOX_MAXLEN) {
                strcpy(&tb->text[tb->cursor], event->ime.text);
                tb->cursor += len;
            }
        }
    }
}

// サンプル: ウィンドウとボタンの描画・イベント処理
void draw_window(struct gui_widget *self) {
    gui_draw_rect(self->x, self->y, self->w, self->h, 0xCCCCCC);
    gui_draw_text(self->x + 4, self->y + 4, "Window", 0x000000);
}

void draw_button(struct gui_widget *self) {
    gui_draw_rect(self->x, self->y, self->w, self->h, 0x8888FF);
    gui_draw_text(self->x + 6, self->y + 6, "Button", 0xFFFFFF);
}

void on_button_event(struct gui_widget *self, struct gui_input_event *event) {
    if (event->type == GUI_EVENT_MOUSE && event->pressed) {
        if (event->x >= self->x && event->x < self->x + self->w && event->y >= self->y && event->y < self->y + self->h) {
            gui_draw_text(self->x, self->y + self->h + 4, "Clicked!", 0xFF0000);
        }
    }
}

int scale_by_dpi(int v) {
    int dpi = gui_get_dpi();
    return v * dpi / 96;
}

int main(void) {
    gui_init();
    gui_widget_init();
    // 高DPI例: 144dpiに設定（1.5倍）
    gui_set_dpi(144);

    struct gui_widget win = {0};
    win.type = GUI_WIDGET_WINDOW;
    win.x = scale_by_dpi(40); win.y = scale_by_dpi(30); win.w = scale_by_dpi(180); win.h = scale_by_dpi(120);
    win.draw = draw_window;
    gui_widget_add(&win, NULL);

    struct gui_widget btn = {0};
    btn.type = GUI_WIDGET_BUTTON;
    btn.x = scale_by_dpi(60); btn.y = scale_by_dpi(70); btn.w = scale_by_dpi(80); btn.h = scale_by_dpi(32);
    btn.draw = draw_button;
    btn.on_event = on_button_event;
    gui_widget_add(&btn, &win);

    // テキストボックス追加
    static struct textbox_data tbdata = {"", 0};
    struct gui_widget textbox = {0};
    textbox.type = GUI_WIDGET_TEXTBOX;
    textbox.x = scale_by_dpi(60); textbox.y = scale_by_dpi(110); textbox.w = scale_by_dpi(100); textbox.h = scale_by_dpi(20);
    textbox.draw = draw_textbox;
    textbox.on_event = on_textbox_event;
    textbox.userdata = &tbdata;
    gui_widget_add(&textbox, &win);

    // リストボックス追加
    static struct listbox_data lbdata = {
        {"Item 1", "Item 2", "Item 3", "Item 4", "Item 5"},
        5, 0
    };
    struct gui_widget listbox = {0};
    listbox.type = GUI_WIDGET_LISTBOX;
    listbox.x = scale_by_dpi(60); listbox.y = scale_by_dpi(140); listbox.w = scale_by_dpi(100); listbox.h = scale_by_dpi(90);
    listbox.draw = draw_listbox;
    listbox.on_event = on_listbox_event;
    listbox.userdata = &lbdata;
    gui_widget_add(&listbox, &win);

    // スクロールバー追加
    static struct scrollbar_data sbdata = {0, 100, 30};
    struct gui_widget scrollbar = {0};
    scrollbar.type = GUI_WIDGET_SCROLLBAR;
    scrollbar.x = scale_by_dpi(170); scrollbar.y = scale_by_dpi(140); scrollbar.w = scale_by_dpi(12); scrollbar.h = scale_by_dpi(SCROLLBAR_HEIGHT);
    scrollbar.draw = draw_scrollbar;
    scrollbar.on_event = on_scrollbar_event;
    scrollbar.userdata = &sbdata;
    gui_widget_add(&scrollbar, &win);

    // チェックボックス追加
    static struct checkbox_data cbdata = {"Check me!", 0};
    struct gui_widget checkbox = {0};
    checkbox.type = GUI_WIDGET_CHECKBOX;
    checkbox.x = scale_by_dpi(60); checkbox.y = scale_by_dpi(240); checkbox.w = scale_by_dpi(100); checkbox.h = scale_by_dpi(20);
    checkbox.draw = draw_checkbox;
    checkbox.on_event = on_checkbox_event;
    checkbox.userdata = &cbdata;
    gui_widget_add(&checkbox, &win);

    // ラジオボタングループ追加
    static int radio_selected = 0;
    static struct radio_data rdata[RADIO_GROUP_MAX] = {
        {"Radio 1", &radio_selected, 0},
        {"Radio 2", &radio_selected, 1},
        {"Radio 3", &radio_selected, 2}
    };
    for (int i = 0; i < RADIO_GROUP_MAX; ++i) {
        struct gui_widget radio = {0};
        radio.type = GUI_WIDGET_RADIO;
        radio.x = scale_by_dpi(180); radio.y = scale_by_dpi(240 + i * 24); radio.w = scale_by_dpi(100); radio.h = scale_by_dpi(20);
        radio.draw = draw_radio;
        radio.on_event = on_radio_event;
        radio.userdata = &rdata[i];
        gui_widget_add(&radio, &win);
    }

    // メニュー追加
    static struct menu_data mdata = {
        {"File", "Edit", "View", "Help"},
        4, 0, 0
    };
    struct gui_widget menu = {0};
    menu.type = GUI_WIDGET_MENU;
    menu.x = scale_by_dpi(40); menu.y = scale_by_dpi(10); menu.w = scale_by_dpi(120); menu.h = scale_by_dpi(20);
    menu.draw = draw_menu;
    menu.on_event = on_menu_event;
    menu.userdata = &mdata;
    gui_widget_add(&menu, &win);

    // 画像表示ウィジェット追加（ダミー画像）
    static struct image_data imgdata;
    for (int i = 0; i < IMAGE_W * IMAGE_H; ++i) imgdata.pixels[i] = (i % 2) ? 0xFFAA00 : 0x00AACC;
    struct gui_widget image = {0};
    image.type = GUI_WIDGET_IMAGE;
    image.x = scale_by_dpi(200); image.y = scale_by_dpi(60); image.w = scale_by_dpi(IMAGE_W); image.h = scale_by_dpi(IMAGE_H);
    image.draw = draw_image;
    image.userdata = &imgdata;
    gui_widget_add(&image, &win);

    // ダイアログ追加
    static struct dialog_data ddata = {"Hello, Dialog!", 1};
    struct gui_widget dialog = {0};
    dialog.type = GUI_WIDGET_DIALOG;
    dialog.x = scale_by_dpi(100); dialog.y = scale_by_dpi(100); dialog.w = scale_by_dpi(120); dialog.h = scale_by_dpi(60);
    dialog.draw = draw_dialog;
    dialog.on_event = on_dialog_event;
    dialog.userdata = &ddata;
    gui_widget_add(&dialog, NULL);

    while (1) {
        gui_widget_draw_all();
        gui_show();
        struct gui_input_event ev;
        if (gui_get_input(&ev)) {
            // タッチイベント例（タッチでボタン押下とみなす）
            if (ev.type == GUI_EVENT_TOUCH && ev.pressed) {
                ev.type = GUI_EVENT_MOUSE; // タッチ→マウス互換処理
            }
            // Tab/Shift+Tabでフォーカス移動
            if (ev.type == GUI_EVENT_KEY && ev.pressed) {
                if (ev.keycode == 9 && !(ev.button & 0x1)) { // Tab
                    gui_widget_focus_next();
                    // ウィンドウ切り替え例
                    current = !current;
                    gui_window_activate(current ? win2_id : win1_id);
                    continue;
                } else if (ev.keycode == 9 && (ev.button & 0x1)) { // Shift+Tab
                    gui_widget_focus_prev();
                    continue;
                }
            }
            gui_widget_dispatch_event(&ev);
        }
        // 適宜sleep等
    }
    return 0;
}
