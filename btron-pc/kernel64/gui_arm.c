static int current_dpi = 96;
void gui_set_dpi(int dpi) { current_dpi = dpi; }
int gui_get_dpi(void) { return current_dpi; }
#include "gui_common.h"
// ARM用の描画・入力実装（ダミー）
void gui_init(void) {}
void gui_draw_pixel(int x, int y, unsigned int color) {}
void gui_draw_rect(int x, int y, int w, int h, unsigned int color) {}
void gui_draw_text(int x, int y, const char *text, unsigned int color) {}
void gui_show(void) {}
int  gui_get_input(struct gui_input_event *event) { return 0; }
