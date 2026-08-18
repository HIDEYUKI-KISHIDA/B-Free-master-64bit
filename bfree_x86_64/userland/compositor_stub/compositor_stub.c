/* Freestanding guest compositor. Reached via init_tramp exec_initrd (APP role).
 * APP Linux sockets: 41 socket, 49 bind, 50 listen, 42 connect, 43 accept.
 * Wayland client is p8test.elf (g1-desk named module, no bfree QPA inject).
 * Built as qt_wl_client.elf (-DWL_QT_CLIENT). Exec env is
 * QT_QPA_PLATFORM=wayland. Not desktop.elf (bfree QPA steals FB). hello.elf
 * stays the fallback if p8test execve returns. Two xdg_toplevels: desk
 * chrome + small app window. App pixels go through /tmp/wlXX vfile tiles
 * (16KB each, no SCM_RIGHTS; a 3.7MB AF_UNIX dump deadlocks). fork (nr 57)
 * lets the parent accept/blit while a long-lived QGuiApplication child
 * lives; vfork (nr 58) is the fallback (parent sleeps until child exit).
 * Compositor blits those tiles; it does not paint the Qt window. Desk
 * chrome still compositor-side (full FB > 64 vfiles). Not product
 * DesktopShell.qml. Real QGuiApplication is qt_wl_hello.elf mapped as
 * p8test.elf when the guest Qt prefix can link it; otherwise this C
 * client stays in the slot.
 * Do not drop QT_QPA_PLATFORM=bfree on daily bfree.iso. Do not GUI_FIRST.
 */
#define BFREE_FB0_FD 0x2000
#define WL_SHM_FORMAT_XRGB8888 1
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define PROT_READ 1
#define PROT_WRITE 2
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define AF_UNIX 1
#define SOCK_STREAM 1
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_PIPE 22
#define SYS_DUP2 33
#define SYS_SOCKET 41
#define SYS_CONNECT 42
#define SYS_ACCEPT 43
#define SYS_BIND 49
#define SYS_LISTEN 50
#define SYS_FORK 57
#define SYS_VFORK 58
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_WAITPID 61
#define WNOHANG 1
#define SYS_FTRUNCATE 77
#define SYS_MSYNC 26
#define SYS_MEMFD 319
#define WL_SHM_FD 8
#define WL_TILE_BYTES 16384
#define WL_APP_MAX_BYTES (480U * 320U * 4U)
#define MS_SYNC 4
/* Cap only the mmap length. Do not put this in BSS — 1024x768 NOBITS hung load_elf. */
#define WL_SURF_MAX_W 1024
#define WL_SURF_MAX_H 768

struct fbinfo {
    void *addr;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned char bpp;
    unsigned char pad[3];
    int ready;
};

#define WL_VIEW_N 2

struct wl_view {
    unsigned int buf_off;
    unsigned int buf_w;
    unsigned int buf_h;
    unsigned int buf_stride;
    unsigned int buf_format;
    unsigned int attached;
    unsigned int committed;
    int x;
    int y;
};

struct wl_state {
    unsigned char *fb;
    unsigned int fb_pitch;
    unsigned int fb_w;
    unsigned int fb_h;
    unsigned char *pool;
    unsigned int pool_size;
    struct wl_view view[WL_VIEW_N];
    unsigned int xdg;
};

static long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long rax __asm__("rax") = n;
    register long rdi __asm__("rdi") = a1;
    register long rsi __asm__("rsi") = a2;
    register long rdx __asm__("rdx") = a3;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return rax;
}

static void serial(const char *s, unsigned long n)
{
    (void)sys6(24, (long)s, (long)n, 0, 0, 0, 0);
}

static void serial_hex(const char *label, long v)
{
    char buf[48];
    unsigned long i = 0;
    unsigned long x;
    int sh;
    while (label[i] && i < 24) {
        buf[i] = label[i];
        i++;
    }
    buf[i++] = '0';
    buf[i++] = 'x';
    x = (unsigned long)v;
    for (sh = 60; sh >= 0; sh -= 4) {
        unsigned d = (unsigned)((x >> sh) & 0xFUL);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    buf[i++] = '\n';
    serial(buf, i);
}

static void put_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned int get_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned wl_put_hdr(unsigned char *p, unsigned int id, unsigned int op, unsigned int nbytes)
{
    put_u32(p, id);
    put_u32(p + 4, (nbytes << 16) | (op & 0xFFFFu));
    return nbytes;
}

static void fill_rect(unsigned char *fb, unsigned int pitch, unsigned int fb_w, unsigned int fb_h,
                      unsigned int x0, unsigned int y0, unsigned int w, unsigned int h, unsigned int color)
{
    unsigned int y;
    unsigned int x;
    if (x0 >= fb_w || y0 >= fb_h) {
        return;
    }
    if (x0 + w > fb_w) {
        w = fb_w - x0;
    }
    if (y0 + h > fb_h) {
        h = fb_h - y0;
    }
    for (y = 0; y < h; y++) {
        unsigned int *row = (unsigned int *)(fb + (unsigned long)(y0 + y) * pitch);
        for (x = 0; x < w; x++) {
            row[x0 + x] = color;
        }
    }
}

static void blit_view(struct wl_state *st, unsigned vi)
{
    struct wl_view *v;
    unsigned int x0;
    unsigned int y0;
    unsigned int y;
    unsigned int x;
    if (vi >= WL_VIEW_N) {
        return;
    }
    v = &st->view[vi];
    if (!st->fb || !st->pool || !v->attached || v->buf_format != WL_SHM_FORMAT_XRGB8888) {
        return;
    }
    if (v->buf_w == 0 || v->buf_h == 0 || v->buf_stride < v->buf_w * 4U) {
        return;
    }
    if (v->buf_off + v->buf_stride * v->buf_h > st->pool_size) {
        return;
    }
    x0 = (unsigned int)(v->x < 0 ? 0 : v->x);
    y0 = (unsigned int)(v->y < 0 ? 0 : v->y);
    for (y = 0; y < v->buf_h && (y0 + y) < st->fb_h; y++) {
        unsigned int *dst = (unsigned int *)(st->fb + (unsigned long)(y0 + y) * st->fb_pitch);
        unsigned int *src = (unsigned int *)(st->pool + v->buf_off + (unsigned long)y * v->buf_stride);
        unsigned int limit = v->buf_w;
        if (x0 + limit > st->fb_w) {
            limit = st->fb_w - x0;
        }
        for (x = 0; x < limit; x++) {
            dst[x0 + x] = src[x];
        }
    }
    v->committed = 1;
}

static void blit_shm(struct wl_state *st)
{
    unsigned i;
    unsigned any = 0;
    for (i = 0; i < WL_VIEW_N; i++) {
        if (st->view[i].attached) {
            blit_view(st, i);
            any = 1;
        }
    }
    if (any) {
        st->xdg = 1;
    }
}

static unsigned wl_committed(struct wl_state *st)
{
    unsigned i;
    for (i = 0; i < WL_VIEW_N; i++) {
        if (st->view[i].committed) {
            return 1;
        }
    }
    return 0;
}

static void __attribute__((unused)) blit_shm_rect(struct wl_state *st, int x0, int y0, int rw, int rh)
{
    int y;
    int x;
    int x1;
    int y1;
    if (!st->fb || !st->pool || !st->view[0].attached ||
        st->view[0].buf_format != WL_SHM_FORMAT_XRGB8888) {
        return;
    }
    if (rw <= 0 || rh <= 0) {
        return;
    }
    if (x0 < 0) {
        rw += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        rh += y0;
        y0 = 0;
    }
    if (x0 >= (int)st->fb_w || y0 >= (int)st->fb_h) {
        return;
    }
    if (x0 + rw > (int)st->fb_w) {
        rw = (int)st->fb_w - x0;
    }
    if (y0 + rh > (int)st->fb_h) {
        rh = (int)st->fb_h - y0;
    }
    if (rw <= 0 || rh <= 0) {
        return;
    }
    x1 = x0 + rw;
    y1 = y0 + rh;
    for (y = y0; y < y1; y++) {
        unsigned int *dst;
        unsigned int *src;
        if ((unsigned)y >= st->view[0].buf_h) {
            break;
        }
        dst = (unsigned int *)(st->fb + (unsigned long)y * st->fb_pitch);
        src = (unsigned int *)(st->pool + st->view[0].buf_off + (unsigned long)y * st->view[0].buf_stride);
        for (x = x0; x < x1 && (unsigned)x < st->view[0].buf_w; x++) {
            dst[x] = src[x];
        }
    }
}

/* Same layout as kernel bfree_raw_input_event_t. BSS — nr 0 vs Linux read
 * needs a mapped ptr >= 0x100000. Stack &ev has been unreliable. */
struct poll_ev {
    int type;
    unsigned int keycode;
    int mouse_x;
    int mouse_y;
    unsigned int mouse_btn;
};

static struct poll_ev g_poll;

static int glyph_row(char c, int row);
static unsigned cstr_n(const char *s);
static unsigned desk_bar(unsigned int h);
static unsigned str_px(const char *s, int scale);

#define DESK_N 16
#define WIN_MAX 4
#define WIN_TITLE_H 38
static const char *const g_acro[DESK_N] = {
    "EX", "VW", "TE", "SM", "SS", "DI", "AS", "NO",
    "CL", "CA", "PB", "NM", "BA", "SO", "JI", "TR"
};
static const char *const g_title[DESK_N] = {
    "Explorer", "Viewer", "Terminal", "SystemMonitor", "SystemSettings", "Discover",
    "AppStore", "Notification", "ClockApplet", "Calculator", "PaintBoard",
    "NetworkManager", "BatteryManager", "SoundManager", "JapaneseIME", "Trash"
};
static const unsigned int g_accent[DESK_N] = {
    0x001D4ED8UL, 0x000F766EUL, 0x00C2410CUL, 0x007C3AEDUL, 0x000F766EUL, 0x000D9488UL,
    0x001E3A8AUL, 0x007C3AEDUL, 0x00C2410CUL, 0x001D4ED8UL, 0x000D9488UL, 0x001D4ED8UL,
    0x000F766EUL, 0x00BE185DUL, 0x007C3AEDUL, 0x00475569UL
};

struct stub_win {
    int open;
    int app;
    int x;
    int y;
    int w;
    int h;
    int z;
    int minimized;
    int maximized;
    int rx;
    int ry;
    int rw;
    int rh;
    unsigned out_n;
    unsigned scroll;
    char out[720];
};
static struct stub_win g_wins[WIN_MAX];
static int g_win_n;
static int g_zseq;
static int g_focus;
static unsigned int g_prev_btn;
static int g_start_open;
static unsigned g_dw;
static unsigned g_dh;
static int g_wm_mode;
static int g_wm_win;
static int g_wm_gx;
static int g_wm_gy;
static int g_wm_ox;
static int g_wm_oy;
static int g_wm_ow;
static int g_wm_oh;
static char g_tline[48];
static unsigned g_tlen;

#define HIT_NONE 0
#define HIT_CLOSE 1
#define HIT_MAX 2
#define HIT_MIN 3
#define HIT_TITLE 4
#define HIT_SE 5
#define HIT_CLIENT 6
#define HIT_SCROLL 7
#define WIN_MIN_W 280
#define WIN_MIN_H 180

static void win_refocus(void);
static void win_raise(int wi);
static void win_close(int wi);
static void desk_present(struct wl_state *st);
static long run_busybox(char *out, unsigned cap, unsigned *out_n, const char *a1, const char *a2);
static void buf_put(char *d, unsigned cap, unsigned *n, const char *s, unsigned sn);
static unsigned term_count_rows(const char *s, unsigned n);
static int term_max_lines(const struct stub_win *w);

static unsigned cstr_n(const char *s)
{
    unsigned n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static unsigned desk_bar(unsigned int h)
{
    return (h > 52U) ? 52U : (h / 6U);
}

/* X11 left_ptr / Windows arrow / Qt ArrowCursor lookalike. Host DesktopShell.qml
 * never painted a sprite — it used the Ubuntu/Yaru system pointer. 0=skip 1=ink 2=fill.
 * Hotspot is the tip (1,1). */
#define CUR_W 12
#define CUR_H 19
#define CUR_HX 1
#define CUR_HY 1
static const unsigned char g_cur_bits[CUR_H][CUR_W] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
    {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0},
    {1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0},
    {1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0},
};
static unsigned int g_cur_under[CUR_W * CUR_H];
static int g_cur_have;
static int g_cur_sx;
static int g_cur_sy;
static int g_mx;
static int g_my;

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void cursor_under(unsigned char *fb, unsigned int pitch, unsigned int fb_w, unsigned int fb_h,
                         int x, int y, int save)
{
    int dy;
    int dx;
    for (dy = 0; dy < CUR_H; dy++) {
        for (dx = 0; dx < CUR_W; dx++) {
            int px = x - CUR_HX + dx;
            int py = y - CUR_HY + dy;
            unsigned int *pix;
            if (px < 0 || py < 0 || (unsigned)px >= fb_w || (unsigned)py >= fb_h) {
                continue;
            }
            pix = (unsigned int *)(fb + (unsigned long)py * pitch);
            if (save) {
                g_cur_under[dy * CUR_W + dx] = pix[px];
            } else {
                pix[px] = g_cur_under[dy * CUR_W + dx];
            }
        }
    }
}

static void cursor_draw(unsigned char *fb, unsigned int pitch, unsigned int fb_w, unsigned int fb_h,
                        int x, int y)
{
    int dy;
    int dx;
    for (dy = 0; dy < CUR_H; dy++) {
        for (dx = 0; dx < CUR_W; dx++) {
            unsigned char cell = g_cur_bits[dy][dx];
            int px;
            int py;
            unsigned int *pix;
            if (cell == 0) {
                continue;
            }
            px = x - CUR_HX + dx;
            py = y - CUR_HY + dy;
            if (px < 0 || py < 0 || (unsigned)px >= fb_w || (unsigned)py >= fb_h) {
                continue;
            }
            pix = (unsigned int *)(fb + (unsigned long)py * pitch);
            pix[px] = (cell == 1) ? 0x00111827UL : 0x00FFF1F2UL;
        }
    }
}

static void cursor_place(struct wl_state *st, int x, int y)
{
    if (!st->fb) {
        return;
    }
    if (g_cur_have) {
        cursor_under(st->fb, st->fb_pitch, st->fb_w, st->fb_h, g_cur_sx, g_cur_sy, 0);
    }
    cursor_under(st->fb, st->fb_pitch, st->fb_w, st->fb_h, x, y, 1);
    g_cur_sx = x;
    g_cur_sy = y;
    g_cur_have = 1;
    cursor_draw(st->fb, st->fb_pitch, st->fb_w, st->fb_h, x, y);
}

static void cursor_hide(struct wl_state *st)
{
    if (g_cur_have && st->fb) {
        cursor_under(st->fb, st->fb_pitch, st->fb_w, st->fb_h, g_cur_sx, g_cur_sy, 0);
    }
    g_cur_have = 0;
}

static void fb_text(unsigned char *fb, unsigned int pitch, unsigned int fb_w, unsigned int fb_h,
                    int x, int y, const char *s, unsigned int color, int scale)
{
    int i;
    int row;
    int col;
    int sx;
    int sy;
    if (scale < 1) {
        scale = 1;
    }
    for (i = 0; s[i]; i++) {
        for (row = 0; row < 7; row++) {
            int bits = glyph_row(s[i], row);
            for (col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    for (sy = 0; sy < scale; sy++) {
                        for (sx = 0; sx < scale; sx++) {
                            int px = x + col * scale + sx;
                            int py = y + row * scale + sy;
                            unsigned int *pix;
                            if (px >= 0 && py >= 0 && (unsigned)px < fb_w && (unsigned)py < fb_h) {
                                pix = (unsigned int *)(fb + (unsigned long)py * pitch);
                                pix[px] = color;
                            }
                        }
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static void icon_xy(int i, unsigned int w, unsigned int h, int *ox, int *oy)
{
    unsigned int bar = desk_bar(h);
    if (i == 15) {
        *ox = (int)w - 16 - 76;
        *oy = (int)h - (int)bar - 12 - 96;
    } else {
        *ox = 28 + (i % 6) * 88;
        *oy = 36 + (i / 6) * 100;
    }
}

static int hit_icon(int mx, int my, unsigned int w, unsigned int h)
{
    int i;
    for (i = 0; i < DESK_N; i++) {
        int x;
        int y;
        if (i == 8) {
            continue;
        }
        icon_xy(i, w, h, &x, &y);
        if (mx >= x && mx < x + 88 && my >= y && my < y + 96) {
            return i;
        }
    }
    return -1;
}

static int hit_start(int mx, int my, unsigned int w, unsigned int h)
{
    unsigned int bar = desk_bar(h);
    int x0 = 8;
    int y0 = (int)h - (int)bar + 6;
    (void)w;
    return (mx >= x0 && mx < x0 + 56 && my >= y0 && my < y0 + 40) ? 1 : 0;
}

static int hit_search(int mx, int my, unsigned int w, unsigned int h)
{
    unsigned int bar = desk_bar(h);
    int x0 = 72;
    int y0 = (int)h - (int)bar + 10;
    (void)w;
    return (mx >= x0 && mx < x0 + 220 && my >= y0 && my < y0 + 32) ? 1 : 0;
}

static void start_geom(unsigned int w, unsigned int h, int *x, int *y, int *pw, int *ph)
{
    unsigned int bar = desk_bar(h);
    *pw = 260;
    *ph = 360;
    *x = 8;
    *y = (int)h - (int)bar - 8 - *ph;
    if (*y < 8) {
        *y = 8;
    }
    if (*x + *pw > (int)w) {
        *pw = (int)w - *x - 8;
    }
}

static int start_id_at_row(int row)
{
    int i;
    int seen = 0;
    if (row < 0) {
        return -1;
    }
    for (i = 0; i < DESK_N; i++) {
        if (i == 8) {
            continue;
        }
        if (seen == row) {
            return i;
        }
        seen++;
    }
    return -1;
}

static int hit_start_item(int mx, int my, unsigned int w, unsigned int h)
{
    int x;
    int y;
    int pw;
    int ph;
    int row;
    if (!g_start_open) {
        return -1;
    }
    start_geom(w, h, &x, &y, &pw, &ph);
    if (mx < x + 8 || mx >= x + pw - 8 || my < y + 36 || my >= y + ph - 8) {
        return -1;
    }
    row = (my - (y + 36)) / 22;
    return start_id_at_row(row);
}

static int hit_win_top(int mx, int my)
{
    int best = -1;
    int bz = -1;
    int i;
    for (i = 0; i < WIN_MAX; i++) {
        if (!g_wins[i].open || g_wins[i].minimized) {
            continue;
        }
        if (mx < g_wins[i].x || my < g_wins[i].y) {
            continue;
        }
        if (mx >= g_wins[i].x + g_wins[i].w || my >= g_wins[i].y + g_wins[i].h) {
            continue;
        }
        if (g_wins[i].z > bz) {
            bz = g_wins[i].z;
            best = i;
        }
    }
    return best;
}

static int hit_part(int wi, int mx, int my)
{
    struct stub_win *w;
    if (wi < 0 || !g_wins[wi].open || g_wins[wi].minimized) {
        return HIT_NONE;
    }
    w = &g_wins[wi];
    if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) {
        return HIT_NONE;
    }
    if (my < w->y + WIN_TITLE_H) {
        if (mx >= w->x + w->w - 40) {
            return HIT_CLOSE;
        }
        if (mx >= w->x + w->w - 76) {
            return HIT_MAX;
        }
        if (mx >= w->x + w->w - 112) {
            return HIT_MIN;
        }
        return HIT_TITLE;
    }
    if (!w->maximized && mx >= w->x + w->w - 16 && my >= w->y + w->h - 16) {
        return HIT_SE;
    }
    if (w->app == 2 && mx >= w->x + w->w - 14 && my >= w->y + WIN_TITLE_H) {
        return HIT_SCROLL;
    }
    return HIT_CLIENT;
}

static void win_clamp(struct stub_win *w)
{
    int bar = (int)desk_bar(g_dh);
    int desk_h = (int)g_dh - bar;
    if (desk_h < WIN_MIN_H) {
        desk_h = WIN_MIN_H;
    }
    if (w->w < WIN_MIN_W) {
        w->w = WIN_MIN_W;
    }
    if (w->h < WIN_MIN_H) {
        w->h = WIN_MIN_H;
    }
    if (w->w > (int)g_dw) {
        w->w = (int)g_dw;
    }
    if (w->h > desk_h) {
        w->h = desk_h;
    }
    if (w->x < 0) {
        w->x = 0;
    }
    if (w->y < 0) {
        w->y = 0;
    }
    if (w->x + w->w > (int)g_dw) {
        w->x = (int)g_dw - w->w;
    }
    if (w->y + w->h > desk_h) {
        w->y = desk_h - w->h;
    }
}

static void win_minimize(int wi)
{
    if (wi < 0 || !g_wins[wi].open) {
        return;
    }
    g_wins[wi].minimized = 1;
    if (g_wm_win == wi) {
        g_wm_mode = 0;
        g_wm_win = -1;
    }
    serial("[wl] wm min\n", 12);
    win_refocus();
}

static void win_maximize(int wi)
{
    struct stub_win *w;
    int bar;
    if (wi < 0 || !g_wins[wi].open) {
        return;
    }
    w = &g_wins[wi];
    if (w->maximized) {
        w->maximized = 0;
        w->x = w->rx;
        w->y = w->ry;
        w->w = w->rw;
        w->h = w->rh;
        win_clamp(w);
        serial("[wl] wm restore\n", 16);
    } else {
        w->rx = w->x;
        w->ry = w->y;
        w->rw = w->w;
        w->rh = w->h;
        w->maximized = 1;
        w->minimized = 0;
        bar = (int)desk_bar(g_dh);
        w->x = 0;
        w->y = 0;
        w->w = (int)g_dw;
        w->h = (int)g_dh - bar;
        serial("[wl] wm max\n", 12);
    }
    win_raise(wi);
}

static void wm_begin(int mode, int wi, int mx, int my)
{
    struct stub_win *w;
    if (wi < 0 || !g_wins[wi].open) {
        return;
    }
    w = &g_wins[wi];
    if (w->maximized && mode == 2) {
        return;
    }
    g_wm_mode = mode;
    g_wm_win = wi;
    g_wm_gx = mx;
    g_wm_gy = my;
    g_wm_ox = w->x;
    g_wm_oy = w->y;
    g_wm_ow = w->w;
    g_wm_oh = w->h;
    win_raise(wi);
    if (mode == 1) {
        serial("[wl] wm drag\n", 13);
    } else if (mode == 2) {
        serial("[wl] wm resize\n", 15);
    } else {
        serial("[wl] term scroll\n", 17);
    }
}

static void wm_apply(int mx, int my)
{
    struct stub_win *w;
    int dx;
    int dy;
    if (g_wm_mode == 0 || g_wm_win < 0) {
        return;
    }
    w = &g_wins[g_wm_win];
    dx = mx - g_wm_gx;
    dy = my - g_wm_gy;
    if (g_wm_mode == 1) {
        w->x = g_wm_ox + dx;
        w->y = g_wm_oy + dy;
    } else if (g_wm_mode == 2) {
        w->w = g_wm_ow + dx;
        w->h = g_wm_oh + dy;
    } else if (g_wm_mode == 3) {
        int maxl;
        unsigned nline;
        unsigned total;
        unsigned maxsc;
        int track_h;
        int rel;
        maxl = term_max_lines(w);
        nline = term_count_rows(w->out, w->out_n);
        total = nline + 1U;
        maxsc = total > (unsigned)maxl ? total - (unsigned)maxl : 0;
        track_h = w->h - WIN_TITLE_H;
        if (track_h < 1) {
            track_h = 1;
        }
        rel = my - (w->y + WIN_TITLE_H);
        if (rel < 0) {
            rel = 0;
        }
        if (rel > track_h) {
            rel = track_h;
        }
        w->scroll = maxsc ? (unsigned)rel * maxsc / (unsigned)track_h : 0;
        if (w->scroll > maxsc) {
            w->scroll = maxsc;
        }
        return;
    }
    win_clamp(w);
}

static void wm_end(void)
{
    if (g_wm_mode) {
        serial("[wl] wm end\n", 12);
        g_wm_mode = 0;
        g_wm_win = -1;
    }
}

static int hit_task_slot(int mx, int my)
{
    int bar = (int)desk_bar(g_dh);
    int y0 = (int)g_dh - bar + 10;
    int n = 0;
    int i;
    if (my < y0 || my >= y0 + 32) {
        return -1;
    }
    for (i = 0; i < WIN_MAX; i++) {
        int x0;
        if (!g_wins[i].open) {
            continue;
        }
        x0 = 300 + n * 110;
        n++;
        if (mx >= x0 && mx < x0 + 100) {
            return i;
        }
    }
    return -1;
}

static void out_make_room(char *d, unsigned cap, unsigned *n, unsigned need)
{
    while (*n + need + 1U >= cap && *n > 0) {
        unsigned i = 0;
        while (i < *n && d[i] != '|') {
            i++;
        }
        if (i < *n) {
            i++;
        }
        if (i == 0) {
            i = 1;
        }
        {
            unsigned k;
            for (k = 0; k + i < *n; k++) {
                d[k] = d[k + i];
            }
            *n -= i;
            d[*n] = 0;
        }
    }
}

static unsigned term_count_rows(const char *s, unsigned n)
{
    unsigned i = 0;
    unsigned rows = 0;
    while (i < n) {
        unsigned k = 0;
        while (i < n && s[i] != '|') {
            k++;
            i++;
        }
        if (i < n && s[i] == '|') {
            i++;
        }
        if (k > 0) {
            rows++;
        }
    }
    return rows;
}

static unsigned term_skip_rows(const char *s, unsigned n, unsigned skip)
{
    unsigned i = 0;
    while (skip && i < n) {
        unsigned k = 0;
        while (i < n && s[i] != '|') {
            k++;
            i++;
        }
        if (i < n && s[i] == '|') {
            i++;
        }
        if (k > 0) {
            skip--;
        }
    }
    return i;
}

static int term_max_lines(const struct stub_win *w)
{
    int maxl = (w->h - WIN_TITLE_H - 22) / 14;
    if (maxl < 1) {
        maxl = 1;
    }
    if (maxl > 28) {
        maxl = 28;
    }
    return maxl;
}

static void term_follow(struct stub_win *w)
{
    unsigned nline = term_count_rows(w->out, w->out_n);
    unsigned total = nline + 1U;
    int maxl = term_max_lines(w);
    w->scroll = total > (unsigned)maxl ? total - (unsigned)maxl : 0;
}

static void term_run(struct stub_win *w)
{
    char cmd[24];
    char arg[24];
    char tmp[200];
    unsigned n = 0;
    unsigned i = 0;
    unsigned c = 0;
    unsigned a = 0;
    const char *a2;
    while (i < g_tlen && g_tline[i] == ' ') {
        i++;
    }
    while (i < g_tlen && g_tline[i] != ' ' && c + 1U < sizeof cmd) {
        cmd[c++] = g_tline[i++];
    }
    cmd[c] = 0;
    while (i < g_tlen && g_tline[i] == ' ') {
        i++;
    }
    while (i < g_tlen && a + 1U < sizeof arg) {
        arg[a++] = g_tline[i++];
    }
    arg[a] = 0;
    if (c == 0) {
        g_tlen = 0;
        g_tline[0] = 0;
        return;
    }
    a2 = a ? arg : 0;
    if (cmd[0] == 'l' && cmd[1] == 's' && cmd[2] == 0 && !a2) {
        a2 = "/";
    }
    out_make_room(w->out, (unsigned)sizeof w->out, &w->out_n, c + a + 4U);
    buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, "# ", 2);
    buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, cmd, c);
    if (a) {
        buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, " ", 1);
        buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, arg, a);
    }
    buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, "|", 1);
    tmp[0] = 0;
    (void)run_busybox(tmp, (unsigned)sizeof tmp, &n, cmd, a2);
    out_make_room(w->out, (unsigned)sizeof w->out, &w->out_n, n + 1U);
    buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, tmp, n);
    buf_put(w->out, (unsigned)sizeof w->out, &w->out_n, "|", 1);
    g_tlen = 0;
    g_tline[0] = 0;
    term_follow(w);
    serial("[wl] term run\n", 14);
}

static void term_key(struct wl_state *st, unsigned k)
{
    struct stub_win *w;
    if (g_focus < 0 || !g_wins[g_focus].open || g_wins[g_focus].app != 2) {
        return;
    }
    w = &g_wins[g_focus];
    if (k == 27U) {
        win_close(g_focus);
        desk_present(st);
        return;
    }
    if (k == 13U || k == 10U) {
        term_run(w);
        desk_present(st);
        return;
    }
    if (k == 8U || k == 127U) {
        if (g_tlen > 0) {
            g_tlen--;
            g_tline[g_tlen] = 0;
            term_follow(w);
            desk_present(st);
        }
        return;
    }
    if (k >= 32U && k < 127U && g_tlen + 1U < sizeof g_tline) {
        g_tline[g_tlen++] = (char)k;
        g_tline[g_tlen] = 0;
        term_follow(w);
        desk_present(st);
    }
}

static void win_refocus(void)
{
    int i;
    int best = -1;
    int bz = -1;
    for (i = 0; i < WIN_MAX; i++) {
        if (g_wins[i].open && !g_wins[i].minimized && g_wins[i].z > bz) {
            bz = g_wins[i].z;
            best = i;
        }
    }
    g_focus = best;
}

static void win_raise(int wi)
{
    if (wi < 0 || !g_wins[wi].open) {
        return;
    }
    g_zseq++;
    g_wins[wi].z = g_zseq;
    g_focus = wi;
}

static int g_pipefd[2];
static char g_bb_path[] = "/busybox.elf";
static char g_bb_a1[32];
static char g_bb_a2[32];
static char *g_bb_argv[4];
static char g_bb_chunk[64];
static int g_waitst;

static void copy_str(char *d, unsigned cap, const char *s)
{
    unsigned i = 0;
    if (!s) {
        d[0] = 0;
        return;
    }
    while (s[i] && i + 1U < cap) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

static void buf_put(char *d, unsigned cap, unsigned *n, const char *s, unsigned sn)
{
    unsigned i;
    for (i = 0; i < sn && *n + 1U < cap; i++) {
        char c = s[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            c = '|';
        }
        d[(*n)++] = c;
    }
    d[*n] = 0;
}

/* APP vfork child + private-AS execve. Path busybox.elf is on the daily ISO.
 * Do not execve desktop.elf on g1-desk. */
static long run_busybox(char *out, unsigned cap, unsigned *out_n, const char *a1, const char *a2)
{
    long pid;
    long rc;
    long nread;
    *out_n = 0;
    if (cap) {
        out[0] = 0;
    }
    copy_str(g_bb_a1, sizeof g_bb_a1, a1);
    copy_str(g_bb_a2, sizeof g_bb_a2, a2);
    g_bb_argv[0] = g_bb_a1;
    g_bb_argv[1] = a2 ? g_bb_a2 : 0;
    g_bb_argv[2] = 0;
    g_pipefd[0] = -1;
    g_pipefd[1] = -1;
    rc = sys6(SYS_PIPE, (long)(unsigned long)g_pipefd, 0, 0, 0, 0, 0);
    serial_hex("wl pipe=", rc);
    if (rc != 0) {
        buf_put(out, cap, out_n, "pipe fail", 9);
        return rc;
    }
    serial("[wl] busybox exec\n", 18);
    pid = sys6(SYS_VFORK, 0, 0, 0, 0, 0, 0);
    serial_hex("wl bb vfork=", pid);
    if (pid == 0) {
        (void)sys6(SYS_DUP2, (long)g_pipefd[1], 1, 0, 0, 0, 0);
        (void)sys6(SYS_DUP2, (long)g_pipefd[1], 2, 0, 0, 0, 0);
        (void)sys6(SYS_CLOSE, (long)g_pipefd[0], 0, 0, 0, 0, 0);
        (void)sys6(SYS_CLOSE, (long)g_pipefd[1], 0, 0, 0, 0, 0);
        rc = sys6(SYS_EXECVE, (long)(unsigned long)g_bb_path, (long)(unsigned long)g_bb_argv, 0, 0, 0, 0);
        serial_hex("wl execve=", rc);
        (void)sys6(SYS_EXIT, 1, 0, 0, 0, 0, 0);
        for (;;) {
        }
    }
    if (pid < 0) {
        buf_put(out, cap, out_n, "vfork fail", 10);
        (void)sys6(SYS_CLOSE, (long)g_pipefd[0], 0, 0, 0, 0, 0);
        (void)sys6(SYS_CLOSE, (long)g_pipefd[1], 0, 0, 0, 0, 0);
        return pid;
    }
    (void)sys6(SYS_CLOSE, (long)g_pipefd[1], 0, 0, 0, 0, 0);
    for (;;) {
        nread = sys6(SYS_READ, (long)g_pipefd[0], (long)(unsigned long)g_bb_chunk, 64, 0, 0, 0);
        if (nread <= 0) {
            break;
        }
        buf_put(out, cap, out_n, g_bb_chunk, (unsigned)nread);
    }
    (void)sys6(SYS_CLOSE, (long)g_pipefd[0], 0, 0, 0, 0, 0);
    rc = sys6(SYS_WAITPID, pid, (long)(unsigned long)&g_waitst, 0, 0, 0, 0);
    serial_hex("wl wait=", rc);
    serial_hex("wl cap n=", (long)*out_n);
    if (*out_n == 0) {
        buf_put(out, cap, out_n, "busybox empty", 13);
    }
    return 0;
}

static void win_close(int wi)
{
    if (wi < 0 || !g_wins[wi].open) {
        return;
    }
    g_wins[wi].open = 0;
    g_wins[wi].minimized = 0;
    g_wins[wi].maximized = 0;
    serial("[wl] desk close\n", 16);
    win_refocus();
}

static void win_open(int app)
{
    int slot = -1;
    int i;
    int existing = -1;
    if (app < 0 || app >= DESK_N || app == 8) {
        return;
    }
    for (i = 0; i < WIN_MAX; i++) {
        if (g_wins[i].open && g_wins[i].app == app) {
            existing = i;
            break;
        }
    }
    if (existing >= 0) {
        g_wins[existing].minimized = 0;
        win_raise(existing);
        serial("[wl] desk raise ", 16);
        serial(g_title[app], cstr_n(g_title[app]));
        serial("\n", 1);
        return;
    }
    for (i = 0; i < g_win_n; i++) {
        if (!g_wins[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_win_n >= WIN_MAX) {
            int oldest = 0;
            for (i = 1; i < WIN_MAX; i++) {
                if (g_wins[i].z < g_wins[oldest].z) {
                    oldest = i;
                }
            }
            slot = oldest;
        } else {
            slot = g_win_n;
            g_win_n++;
        }
    }
    g_wins[slot].open = 1;
    g_wins[slot].app = app;
    g_wins[slot].minimized = 0;
    g_wins[slot].maximized = 0;
    if (app == 2) {
        g_wins[slot].w = 640;
        g_wins[slot].h = 420;
        g_wins[slot].x = 100 + (slot % 3) * 40;
        g_wins[slot].y = 40 + (slot % 3) * 24;
    } else if (app == 0) {
        g_wins[slot].w = 720;
        g_wins[slot].h = 480;
        g_wins[slot].x = 80 + (slot % 3) * 24;
        g_wins[slot].y = 48 + (slot % 3) * 20;
    } else {
        g_wins[slot].w = 520;
        g_wins[slot].h = 320;
        g_wins[slot].x = 120 + (slot % 3) * 40;
        g_wins[slot].y = 80 + (slot % 3) * 36;
    }
    g_wins[slot].rx = g_wins[slot].x;
    g_wins[slot].ry = g_wins[slot].y;
    g_wins[slot].rw = g_wins[slot].w;
    g_wins[slot].rh = g_wins[slot].h;
    win_raise(slot);
    serial("[wl] desk open ", 15);
    serial(g_title[app], cstr_n(g_title[app]));
    serial("\n", 1);
    g_wins[slot].out_n = 0;
    g_wins[slot].out[0] = 0;
    g_wins[slot].scroll = 0;
    g_tlen = 0;
    g_tline[0] = 0;
    if (app == 0) {
        (void)run_busybox(g_wins[slot].out, (unsigned)sizeof g_wins[slot].out, &g_wins[slot].out_n,
                          "ls", "/");
    }
}

static void paint_one_win(struct wl_state *st, int wi)
{
    struct stub_win *w = &g_wins[wi];
    unsigned int title = (wi == g_focus) ? 0x00334155UL : 0x00475569UL;
    unsigned int border = (wi == g_focus) ? 0x0064748BUL : 0x0094A3B8UL;
    int bw = (wi == g_focus) ? 2 : 1;
    int app = w->app;
    int client_y;
    if (w->minimized) {
        return;
    }
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + 5), (unsigned)(w->y + 7), (unsigned)w->w, (unsigned)w->h, 0x002A3545UL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)w->x, (unsigned)w->y, (unsigned)w->w, (unsigned)w->h, 0x00F8FAFCUL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)w->x, (unsigned)w->y, (unsigned)w->w, (unsigned)bw, border);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)w->x, (unsigned)(w->y + w->h - bw), (unsigned)w->w, (unsigned)bw, border);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)w->x, (unsigned)w->y, (unsigned)bw, (unsigned)w->h, border);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - bw), (unsigned)w->y, (unsigned)bw, (unsigned)w->h, border);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + bw), (unsigned)(w->y + bw), (unsigned)(w->w - 2 * bw),
              (unsigned)(WIN_TITLE_H - bw), title);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + 8), (unsigned)(w->y + 5), 28, 28, g_accent[app]);
    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 12, w->y + 12, g_acro[app], 0x00F8FAFCUL, 1);
    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 44, w->y + 11, g_title[app], 0x00F8FAFCUL, 2);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 112), (unsigned)(w->y + 6), 32, 26, 0x00406090UL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 102), (unsigned)(w->y + 18), 12, 2, 0x00F8FAFCUL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 76), (unsigned)(w->y + 6), 32, 26, 0x00406090UL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 66), (unsigned)(w->y + 12), 14, 14, 0x00F8FAFCUL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 64), (unsigned)(w->y + 14), 10, 10, 0x00406090UL);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
              (unsigned)(w->x + w->w - 40), (unsigned)(w->y + 6), 32, 26, 0x00B91C1CUL);
    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + w->w - 28, w->y + 12, "X", 0x00F8FAFCUL, 2);
    if (app == 2) {
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + bw), (unsigned)(w->y + WIN_TITLE_H), (unsigned)(w->w - 2 * bw),
                  (unsigned)(w->h - WIN_TITLE_H - bw), 0x000D1B2AUL);
        {
            int top;
            int maxl;
            unsigned nline;
            unsigned total;
            unsigned maxsc;
            unsigned off;
            unsigned row;
            unsigned cols;
            char prompt[56];
            unsigned p = 0;
            int sb_x;
            int sb_y;
            int sb_h;
            top = w->y + WIN_TITLE_H + 10;
            maxl = term_max_lines(w);
            cols = (unsigned)((w->w - 32) / 6);
            if (cols < 8U) {
                cols = 8U;
            }
            if (cols > 80U) {
                cols = 80U;
            }
            nline = term_count_rows(w->out, w->out_n);
            total = nline + 1U;
            maxsc = total > (unsigned)maxl ? total - (unsigned)maxl : 0;
            if (w->scroll > maxsc) {
                w->scroll = maxsc;
            }
            off = term_skip_rows(w->out, w->out_n, w->scroll);
            row = 0;
            while (off < w->out_n && row < (unsigned)maxl && w->scroll + row < nline) {
                char hist[81];
                unsigned k = 0;
                while (off < w->out_n && w->out[off] != '|') {
                    if (k + 1U < cols && k + 1U < sizeof hist) {
                        hist[k++] = w->out[off];
                    }
                    off++;
                }
                if (off < w->out_n && w->out[off] == '|') {
                    off++;
                }
                hist[k] = 0;
                if (k > 0) {
                    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 12,
                            top + (int)row * 14, hist, 0x004ADE80UL, 1);
                    row++;
                }
            }
            if (w->scroll + row >= nline && row < (unsigned)maxl) {
                prompt[p++] = '#';
                prompt[p++] = ' ';
                {
                    unsigned i;
                    for (i = 0; i < g_tlen && p + 1U < sizeof prompt && p < cols; i++) {
                        prompt[p++] = g_tline[i];
                    }
                }
                prompt[p] = 0;
                fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 12,
                        top + (int)row * 14, prompt, 0x00E2E8F0UL, 1);
                fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                          (unsigned)(w->x + 12 + (int)p * 6), (unsigned)(top + (int)row * 14),
                          8, 12, 0x00E2E8F0UL);
            }
            sb_x = w->x + w->w - 12;
            sb_y = w->y + WIN_TITLE_H + 2;
            sb_h = w->h - WIN_TITLE_H - 8;
            if (sb_h < 16) {
                sb_h = 16;
            }
            fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                      (unsigned)sb_x, (unsigned)sb_y, 8, (unsigned)sb_h, 0x00152538UL);
            if (maxsc > 0) {
                int th = (int)((unsigned)sb_h * (unsigned)maxl / total);
                int ty;
                if (th < 12) {
                    th = 12;
                }
                if (th > sb_h) {
                    th = sb_h;
                }
                ty = sb_y + (int)((unsigned)(sb_h - th) * w->scroll / maxsc);
                fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                          (unsigned)sb_x, (unsigned)ty, 8, (unsigned)th, 0x0064748BUL);
            }
        }
    } else if (app == 0) {
        client_y = w->y + WIN_TITLE_H;
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + bw), (unsigned)client_y, (unsigned)(w->w - 2 * bw), 28, 0x00F3F4F6UL);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 12, client_y + 8, "New  Cut  Copy", 0x00334155UL, 1);
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + bw), (unsigned)(client_y + 28), (unsigned)(w->w - 2 * bw), 24, 0x00FFFFFFUL);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, client_y + 34, "/", 0x00334155UL, 1);
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + bw), (unsigned)(client_y + 52), 132,
                  (unsigned)(w->h - WIN_TITLE_H - 52 - bw), 0x00EEF2F6UL);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, client_y + 64, "Home", 0x001D4ED8UL, 1);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, client_y + 84, "persist", 0x00334155UL, 1);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, client_y + 104, "bin", 0x00334155UL, 1);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, client_y + 124, "tmp", 0x00334155UL, 1);
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + 132 + bw), (unsigned)(client_y + 52), (unsigned)(w->w - 132 - 2 * bw),
                  (unsigned)(w->h - WIN_TITLE_H - 52 - bw), 0x00FFFFFFUL);
        {
            unsigned off = 0;
            int line = 0;
            while (off < w->out_n && line < 12) {
                char row[28];
                unsigned k = 0;
                while (k < 24U && off < w->out_n) {
                    char c = w->out[off++];
                    if (c == '|') {
                        break;
                    }
                    row[k++] = c;
                }
                row[k] = 0;
                if (k > 0) {
                    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                              (unsigned)(w->x + 148), (unsigned)(client_y + 60 + line * 22), 18, 16, 0x001D4ED8UL);
                    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 172, client_y + 64 + line * 22,
                            row, 0x001E293BUL, 1);
                }
                line++;
            }
        }
    } else {
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + bw), (unsigned)(w->y + WIN_TITLE_H), (unsigned)(w->w - 2 * bw),
                  (unsigned)(w->h - WIN_TITLE_H - bw), 0x00F1F5F9UL);
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, w->x + 16, w->y + WIN_TITLE_H + 16,
                g_title[app], 0x00334155UL, 1);
    }
    if (!w->maximized) {
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + w->w - 18), (unsigned)(w->y + w->h - 6), 14, 4, 0x001E293BUL);
        fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h,
                  (unsigned)(w->x + w->w - 6), (unsigned)(w->y + w->h - 18), 4, 14, 0x001E293BUL);
    }
}

static void paint_start(struct wl_state *st)
{
    int x;
    int y;
    int pw;
    int ph;
    int row;
    int i;
    if (!g_start_open) {
        return;
    }
    start_geom(st->fb_w, st->fb_h, &x, &y, &pw, &ph);
    fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h, (unsigned)x, (unsigned)y,
              (unsigned)pw, (unsigned)ph, 0x00152538UL);
    fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, x + 16, y + 12, "Start", 0x00E2E8F0UL, 2);
    row = 0;
    for (i = 0; i < DESK_N; i++) {
        if (i == 8) {
            continue;
        }
        fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, x + 16, y + 36 + row * 22,
                g_title[i], 0x00C8DCEDUL, 1);
        row++;
    }
}

static void paint_windows(struct wl_state *st)
{
    int order[WIN_MAX];
    int n = 0;
    int a;
    int b;
    int i;
    for (i = 0; i < WIN_MAX; i++) {
        if (g_wins[i].open && !g_wins[i].minimized) {
            order[n++] = i;
        }
    }
    for (a = 0; a < n; a++) {
        for (b = a + 1; b < n; b++) {
            if (g_wins[order[b]].z < g_wins[order[a]].z) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }
    for (i = 0; i < n; i++) {
        paint_one_win(st, order[i]);
    }
    {
        int bar = (int)desk_bar(st->fb_h);
        int slotn = 0;
        for (i = 0; i < WIN_MAX; i++) {
            int x0;
            if (!g_wins[i].open) {
                continue;
            }
            x0 = 300 + slotn * 110;
            slotn++;
            fill_rect(st->fb, st->fb_pitch, st->fb_w, st->fb_h, (unsigned)x0,
                      (unsigned)(st->fb_h - (unsigned)bar + 10), 100, 32,
                      (i == g_focus && !g_wins[i].minimized) ? 0x001A3060UL : 0x00152538UL);
            fb_text(st->fb, st->fb_pitch, st->fb_w, st->fb_h, x0 + 8,
                    (int)st->fb_h - bar + 20, g_acro[g_wins[i].app], 0x00C8DCEDUL, 1);
        }
    }
    paint_start(st);
}

#define DIRTY_MAX 8
static int g_dirty_x[DIRTY_MAX];
static int g_dirty_y[DIRTY_MAX];
static int g_dirty_w[DIRTY_MAX];
static int g_dirty_h[DIRTY_MAX];
static int g_dirty_n;

static void dirty_add(int x, int y, int w, int h)
{
    if (g_dirty_n >= DIRTY_MAX || w <= 0 || h <= 0) {
        return;
    }
    g_dirty_x[g_dirty_n] = x;
    g_dirty_y[g_dirty_n] = y;
    g_dirty_w[g_dirty_n] = w;
    g_dirty_h[g_dirty_n] = h;
    g_dirty_n++;
}

static void desk_present(struct wl_state *st)
{
    int i;
    int bar;
    cursor_hide(st);
    blit_shm(st);
    g_dirty_n = 0;
    paint_windows(st);
    for (i = 0; i < WIN_MAX; i++) {
        if (g_wins[i].open && !g_wins[i].minimized) {
            dirty_add(g_wins[i].x, g_wins[i].y, g_wins[i].w + 5, g_wins[i].h + 7);
        }
    }
    if (g_start_open) {
        int x;
        int y;
        int pw;
        int ph;
        start_geom(st->fb_w, st->fb_h, &x, &y, &pw, &ph);
        dirty_add(x, y, pw, ph);
    }
    bar = (int)desk_bar(st->fb_h);
    dirty_add(0, (int)st->fb_h - bar, (int)st->fb_w, bar);
    cursor_place(st, g_mx, g_my);
}

static void desk_click(struct wl_state *st, int mx, int my)
{
    int si;
    int wi;
    int hit;
    int part;
    int tslot;
    si = hit_start_item(mx, my, st->fb_w, st->fb_h);
    if (si >= 0) {
        g_start_open = 0;
        win_open(si);
        desk_present(st);
        return;
    }
    if (hit_start(mx, my, st->fb_w, st->fb_h) || hit_search(mx, my, st->fb_w, st->fb_h)) {
        g_start_open = g_start_open ? 0 : 1;
        serial(g_start_open ? "[wl] Start open\n" : "[wl] Start close\n",
               g_start_open ? 16 : 17);
        desk_present(st);
        return;
    }
    tslot = hit_task_slot(mx, my);
    if (tslot >= 0) {
        if (tslot == g_focus && !g_wins[tslot].minimized) {
            win_minimize(tslot);
        } else {
            g_wins[tslot].minimized = 0;
            win_raise(tslot);
        }
        desk_present(st);
        return;
    }
    if (g_start_open) {
        g_start_open = 0;
        serial("[wl] Start close\n", 17);
        desk_present(st);
        return;
    }
    wi = hit_win_top(mx, my);
    if (wi >= 0) {
        part = hit_part(wi, mx, my);
        if (part == HIT_CLOSE) {
            win_close(wi);
            desk_present(st);
            return;
        }
        if (part == HIT_MIN) {
            win_minimize(wi);
            desk_present(st);
            return;
        }
        if (part == HIT_MAX) {
            win_maximize(wi);
            desk_present(st);
            return;
        }
        if (part == HIT_TITLE) {
            if (!g_wins[wi].maximized) {
                wm_begin(1, wi, mx, my);
            } else {
                win_raise(wi);
            }
            desk_present(st);
            return;
        }
        if (part == HIT_SE) {
            wm_begin(2, wi, mx, my);
            desk_present(st);
            return;
        }
        if (part == HIT_SCROLL) {
            wm_begin(3, wi, mx, my);
            wm_apply(mx, my);
            desk_present(st);
            return;
        }
        win_raise(wi);
        desk_present(st);
        return;
    }
    hit = hit_icon(mx, my, st->fb_w, st->fb_h);
    if (hit >= 0) {
        win_open(hit);
        desk_present(st);
    }
}

static void cursor_loop(struct wl_state *st)
{
    static const char curon[] = "[wl] cursor arrow\n";
    static int logged;
    long ret;
    if (st->fb_w == 0 || st->fb_h == 0) {
        for (;;) {
        }
    }
    g_mx = (int)(st->fb_w / 2U);
    g_my = (int)(st->fb_h / 2U);
    g_dw = st->fb_w;
    g_dh = st->fb_h;
    g_prev_btn = 0;
    g_zseq = 1;
    g_focus = -1;
    g_wm_mode = 0;
    g_wm_win = -1;
    cursor_place(st, g_mx, g_my);
    serial(curon, sizeof(curon) - 1);
    for (;;) {
        int n;
        for (n = 0; n < 32; n++) {
            unsigned int btn;
            g_poll.type = 0;
            g_poll.keycode = 0;
            g_poll.mouse_x = 0;
            g_poll.mouse_y = 0;
            g_poll.mouse_btn = 0;
            ret = sys6(0, (long)(unsigned long)&g_poll, 0, 0, 0, 0, 0);
            if (ret <= 0) {
                break;
            }
            if (g_poll.type == 1) {
                term_key(st, g_poll.keycode);
                continue;
            }
            if (g_poll.type != 3) {
                continue;
            }
            g_mx = clamp_i(g_poll.mouse_x, 0, (int)st->fb_w - 1);
            g_my = clamp_i(g_poll.mouse_y, 0, (int)st->fb_h - 1);
            btn = g_poll.mouse_btn;
            if (!logged) {
                serial_hex("wl mouse x=", (long)g_mx);
                serial_hex("wl mouse y=", (long)g_my);
                logged = 1;
            }
            if ((btn & 1U) && !(g_prev_btn & 1U)) {
                desk_click(st, g_mx, g_my);
            } else if (!(btn & 1U) && (g_prev_btn & 1U)) {
                wm_end();
            } else if (g_wm_mode && (btn & 1U)) {
                wm_apply(g_mx, g_my);
                desk_present(st);
            } else if (g_mx != g_cur_sx || g_my != g_cur_sy) {
                cursor_place(st, g_mx, g_my);
            }
            g_prev_btn = btn;
        }
    }
}

static void wl_dispatch(struct wl_state *st, const unsigned char *msg, unsigned int len)
{
    unsigned int off = 0;
    while (off + 8U <= len) {
        unsigned int id = get_u32(msg + off);
        unsigned int word = get_u32(msg + off + 4);
        unsigned int op = word & 0xFFFFu;
        unsigned int sz = word >> 16;
        const unsigned char *a;
        if (sz < 8U || off + sz > len) {
            break;
        }
        a = msg + off + 8;
        if (id == 1 && op == 1) {
            serial("[wl] get_registry\n", 18);
        } else if (id == 2 && op == 0) {
            unsigned int name = get_u32(a);
            serial("[wl] bind\n", 10);
            serial_hex("wl bind name=", (long)name);
            if (name == 3U) {
                serial("[wl] bind xdg_wm_base\n", 23);
            }
        } else if (id == 3 && op == 0) {
            serial("[wl] create_surface\n", 21);
        } else if (id == 8 && op == 2) {
            serial("[wl] get_xdg_surface\n", 22);
        } else if (id == 9 && op == 1) {
            st->xdg = 1;
            st->view[0].x = 0;
            st->view[0].y = 0;
            serial("[wl] get_toplevel desk\n", 24);
        } else if (id == 12 && op == 1) {
            st->xdg = 1;
            st->view[1].x = 540;
            st->view[1].y = 48;
            serial("[wl] get_toplevel\n", 19);
        } else if ((id == 10 || id == 13) && op == 2) {
            serial("[wl] xdg set_title\n", 20);
        } else if ((id == 9 || id == 12) && op == 4) {
            serial("[wl] xdg ack_configure\n", 24);
        } else if (id == 4 && op == 0) {
            st->pool_size = get_u32(a + 4);
            serial("[wl] create_pool\n", 18);
            serial_hex("wl pool size=", (long)st->pool_size);
        } else if (id == 6 && op == 0) {
            unsigned int nid = get_u32(a);
            unsigned vi = (nid == 14U) ? 1U : 0U;
            st->view[vi].buf_off = get_u32(a + 4);
            st->view[vi].buf_w = get_u32(a + 8);
            st->view[vi].buf_h = get_u32(a + 12);
            st->view[vi].buf_stride = get_u32(a + 16);
            st->view[vi].buf_format = get_u32(a + 20);
            serial("[wl] create_buffer\n", 20);
            serial_hex("wl buf w=", (long)st->view[vi].buf_w);
            serial_hex("wl buf h=", (long)st->view[vi].buf_h);
        } else if ((id == 5 || id == 11) && op == 1) {
            unsigned vi = (id == 11U) ? 1U : 0U;
            st->view[vi].attached = (get_u32(a) != 0);
            serial("[wl] attach\n", 13);
        } else if ((id == 5 || id == 11) && op == 6) {
            serial("[wl] commit\n", 13);
            if (id == 5) {
                serial("[wl] xdg desk commit\n", 22);
            } else {
                serial("[wl] xdg toplevel commit\n", 26);
            }
            blit_shm(st);
        }
        off += sz;
    }
}

static void pool_rect(unsigned int *p, unsigned int w, unsigned int h,
                      unsigned int x0, unsigned int y0, unsigned int rw, unsigned int rh,
                      unsigned int color)
{
    unsigned int y;
    unsigned int x;
    if (x0 >= w || y0 >= h) {
        return;
    }
    if (x0 + rw > w) {
        rw = w - x0;
    }
    if (y0 + rh > h) {
        rh = h - y0;
    }
    for (y = 0; y < rh; y++) {
        unsigned int *row = p + (unsigned long)(y0 + y) * w;
        for (x = 0; x < rw; x++) {
            row[x0 + x] = color;
        }
    }
}

/* 5x7 glyphs — same bitmaps as guest_main.cpp fb_draw_text. */
static int glyph_row(char c, int row)
{
    static const unsigned char A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const unsigned char B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const unsigned char C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const unsigned char D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const unsigned char E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const unsigned char F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const unsigned char G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const unsigned char H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const unsigned char I[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const unsigned char J[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    static const unsigned char K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const unsigned char L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const unsigned char M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const unsigned char N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const unsigned char O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const unsigned char P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const unsigned char Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const unsigned char R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const unsigned char S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const unsigned char T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const unsigned char U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const unsigned char V[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const unsigned char W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    static const unsigned char X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const unsigned char Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const unsigned char Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const unsigned char la[7] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F};
    static const unsigned char lb[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E};
    static const unsigned char lc[7] = {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E};
    static const unsigned char ld[7] = {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F};
    static const unsigned char le[7] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E};
    static const unsigned char lf[7] = {0x06, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08};
    static const unsigned char lg[7] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E};
    static const unsigned char lh[7] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static const unsigned char li[7] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E};
    static const unsigned char lj[7] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C};
    static const unsigned char lk[7] = {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12};
    static const unsigned char ll[7] = {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const unsigned char lm[7] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15};
    static const unsigned char ln[7] = {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11};
    static const unsigned char lo[7] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E};
    static const unsigned char lp[7] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10};
    static const unsigned char lq[7] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01};
    static const unsigned char lr[7] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10};
    static const unsigned char ls[7] = {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E};
    static const unsigned char lt[7] = {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06};
    static const unsigned char lu[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D};
    static const unsigned char lv[7] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const unsigned char lw[7] = {0x00, 0x00, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const unsigned char lx[7] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11};
    static const unsigned char ly[7] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E};
    static const unsigned char lz[7] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F};
    static const unsigned char n0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const unsigned char n1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const unsigned char n2[7] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F};
    static const unsigned char col[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const unsigned char st[7] = {0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00};
    static const unsigned char mn[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const unsigned char sl[7] = {0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x10};
    static const unsigned char dt[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const unsigned char hs[7] = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A};
    const unsigned char *g = 0;
    if (row < 0 || row > 6) {
        return 0;
    }
    if (c == 'A') {
        g = A;
    } else if (c == 'B') {
        g = B;
    } else if (c == 'C') {
        g = C;
    } else if (c == 'D') {
        g = D;
    } else if (c == 'E') {
        g = E;
    } else if (c == 'F') {
        g = F;
    } else if (c == 'G') {
        g = G;
    } else if (c == 'H') {
        g = H;
    } else if (c == 'I') {
        g = I;
    } else if (c == 'J') {
        g = J;
    } else if (c == 'K') {
        g = K;
    } else if (c == 'L') {
        g = L;
    } else if (c == 'M') {
        g = M;
    } else if (c == 'N') {
        g = N;
    } else if (c == 'O') {
        g = O;
    } else if (c == 'P') {
        g = P;
    } else if (c == 'Q') {
        g = Q;
    } else if (c == 'R') {
        g = R;
    } else if (c == 'S') {
        g = S;
    } else if (c == 'T') {
        g = T;
    } else if (c == 'U') {
        g = U;
    } else if (c == 'V') {
        g = V;
    } else if (c == 'W') {
        g = W;
    } else if (c == 'X') {
        g = X;
    } else if (c == 'Y') {
        g = Y;
    } else if (c == 'Z') {
        g = Z;
    } else if (c == 'a') {
        g = la;
    } else if (c == 'b') {
        g = lb;
    } else if (c == 'c') {
        g = lc;
    } else if (c == 'd') {
        g = ld;
    } else if (c == 'e') {
        g = le;
    } else if (c == 'f') {
        g = lf;
    } else if (c == 'g') {
        g = lg;
    } else if (c == 'h') {
        g = lh;
    } else if (c == 'i') {
        g = li;
    } else if (c == 'j') {
        g = lj;
    } else if (c == 'k') {
        g = lk;
    } else if (c == 'l') {
        g = ll;
    } else if (c == 'm') {
        g = lm;
    } else if (c == 'n') {
        g = ln;
    } else if (c == 'o') {
        g = lo;
    } else if (c == 'p') {
        g = lp;
    } else if (c == 'q') {
        g = lq;
    } else if (c == 'r') {
        g = lr;
    } else if (c == 's') {
        g = ls;
    } else if (c == 't') {
        g = lt;
    } else if (c == 'u') {
        g = lu;
    } else if (c == 'v') {
        g = lv;
    } else if (c == 'w') {
        g = lw;
    } else if (c == 'x') {
        g = lx;
    } else if (c == 'y') {
        g = ly;
    } else if (c == 'z') {
        g = lz;
    } else if (c == '0') {
        g = n0;
    } else if (c == '1') {
        g = n1;
    } else if (c == '2') {
        g = n2;
    } else if (c == ':') {
        g = col;
    } else if (c == '*') {
        g = st;
    } else if (c == '-') {
        g = mn;
    } else if (c == '/') {
        g = sl;
    } else if (c == '.') {
        g = dt;
    } else if (c == '#') {
        g = hs;
    } else {
        return 0;
    }
    return (int)g[row];
}

static void shm_text(unsigned int *p, unsigned int w, unsigned int h, int x, int y,
                     const char *s, unsigned int color, int scale)
{
    int i;
    int row;
    int col;
    int sx;
    int sy;
    if (scale < 1) {
        scale = 1;
    }
    for (i = 0; s[i]; i++) {
        for (row = 0; row < 7; row++) {
            int bits = glyph_row(s[i], row);
            for (col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    for (sy = 0; sy < scale; sy++) {
                        for (sx = 0; sx < scale; sx++) {
                            int px = x + col * scale + sx;
                            int py = y + row * scale + sy;
                            if (px >= 0 && py >= 0 && (unsigned)px < w && (unsigned)py < h) {
                                p[(unsigned)py * w + (unsigned)px] = color;
                            }
                        }
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

static unsigned str_px(const char *s, int scale)
{
    unsigned n = 0;
    while (s[n]) {
        n++;
    }
    return n * 6U * (unsigned)scale;
}

/* Fullscreen xdg desk surface: icons + taskbar. Compositor only composites.
 * Not product DesktopShell.qml. */
static void draw_desk_chrome(unsigned int *p, unsigned int w, unsigned int h)
{
    unsigned int bar = desk_bar(h);
    unsigned int i;
    unsigned int tile = 48;
    int x;
    int y;
    pool_rect(p, w, h, 0, 0, w, h, 0x007A8FA8UL);
    pool_rect(p, w, h, 0, h - bar, w, bar, 0x000D1B2AUL);
    pool_rect(p, w, h, 0, h - bar, w, 1, 0x001E3050UL);
    pool_rect(p, w, h, 8, h - bar + 6, 56, 40, 0x001A3060UL);
    shm_text(p, w, h, 16, (int)(h - bar + 18), "Start", 0x00C8DCEDUL, 1);
    pool_rect(p, w, h, 72, h - bar + 10, 220, 32, 0x001A2D42UL);
    shm_text(p, w, h, 84, (int)(h - bar + 18), "Q", 0x0088AAC0UL, 1);
    shm_text(p, w, h, 100, (int)(h - bar + 20), "Search", 0x00557090UL, 1);
    if (w > 260U) {
        pool_rect(p, w, h, w - 264, h - bar + 10, 36, 32, 0x00152538UL);
        shm_text(p, w, h, (int)(w - 256), (int)(h - bar + 20), "Net", 0x0088AAC0UL, 1);
        pool_rect(p, w, h, w - 224, h - bar + 10, 28, 32, 0x00152538UL);
        shm_text(p, w, h, (int)(w - 216), (int)(h - bar + 20), "N", 0x0088AAC0UL, 1);
        pool_rect(p, w, h, w - 192, h - bar + 10, 28, 32, 0x00152538UL);
        shm_text(p, w, h, (int)(w - 184), (int)(h - bar + 20), "*", 0x0088AAC0UL, 1);
        pool_rect(p, w, h, w - 156, h - bar + 10, 144, 32, 0x00152538UL);
        shm_text(p, w, h, (int)(w - 140), (int)(h - bar + 20), "12:00 AM", 0x00E2E8F0UL, 1);
    }
    for (i = 0; i < 16U; i++) {
        if (i == 8U) {
            continue;
        }
        if (i == 15U) {
            x = (int)w - 16 - 76;
            y = (int)h - (int)bar - 12 - 96;
        } else {
            x = 28 + (int)(i % 6U) * 88;
            y = 36 + (int)(i / 6U) * 100;
        }
        if (x < 0 || y < 0 || (unsigned)(x + 62) >= w || (unsigned)(y + 70) + bar >= h) {
            continue;
        }
        pool_rect(p, w, h, (unsigned)(x + 14), (unsigned)y, tile, tile, g_accent[i]);
        shm_text(p, w, h, x + 14 + (int)(tile - str_px(g_acro[i], 2)) / 2, y + 16, g_acro[i],
                 0x00F8FAFCUL, 2);
        {
            int tw = (int)str_px(g_title[i], 1);
            int tx = x + (76 - tw) / 2;
            if (tx < x) {
                tx = x;
            }
            shm_text(p, w, h, tx, y + 54, g_title[i], 0x00E8EEF5UL, 1);
        }
    }
}

/* Second xdg_toplevel: 480x320 app window. Qt Wayland client slot, not DesktopShell.qml.
 * Client-only marks ("shm" + rose bar) prove compositor did not paint this. */
static void draw_xdg_window(unsigned int *p, unsigned int w, unsigned int h)
{
    pool_rect(p, w, h, 0, 0, w, h, 0x00F1F5F9UL);
    pool_rect(p, w, h, 0, 0, w, 36, 0x002F6B3AUL);
    shm_text(p, w, h, 12, 12, "Qt", 0x00F8FAFCUL, 2);
    shm_text(p, w, h, 16, 56, "wayland", 0x000F172AUL, 2);
#ifdef WL_AS_CLIENT
    pool_rect(p, w, h, 16, 92, 72, 8, 0x00E11D48UL);
    shm_text(p, w, h, 16, 108, "shm", 0x000F172AUL, 2);
#endif
}

/* Two xdg_toplevels, one pool.
 *   5/9/10 + buf 7  = fullscreen desk (bar/icons)
 *   11/12/13 + buf 14 = 480x320 app window
 * Keep compositor=3 shm=4 pool=6 — dispatch already uses those ids. */
static unsigned wl_client_build(unsigned char *m, unsigned int dw, unsigned int dh,
                                unsigned int aw, unsigned int ah)
{
    unsigned int o = 0;
    unsigned int n;
    unsigned int psz = dw * dh * 4U + aw * ah * 4U;
    unsigned int poff = dw * dh * 4U;
    /* wl_display.get_registry(new_id=2) */
    n = 12;
    wl_put_hdr(m + o, 1, 1, n);
    put_u32(m + o + 8, 2);
    o += n;
    /* wl_registry.bind(name=1, "wl_compositor", ver=4, id=3) */
    n = 40;
    wl_put_hdr(m + o, 2, 0, n);
    put_u32(m + o + 8, 1);
    put_u32(m + o + 12, 14);
    m[o + 16] = 'w';
    m[o + 17] = 'l';
    m[o + 18] = '_';
    m[o + 19] = 'c';
    m[o + 20] = 'o';
    m[o + 21] = 'm';
    m[o + 22] = 'p';
    m[o + 23] = 'o';
    m[o + 24] = 's';
    m[o + 25] = 'i';
    m[o + 26] = 't';
    m[o + 27] = 'o';
    m[o + 28] = 'r';
    m[o + 29] = 0;
    m[o + 30] = 0;
    m[o + 31] = 0;
    put_u32(m + o + 32, 4);
    put_u32(m + o + 36, 3);
    o += n;
    /* wl_registry.bind(name=3, "xdg_wm_base", ver=1, id=8) */
    n = 36;
    wl_put_hdr(m + o, 2, 0, n);
    put_u32(m + o + 8, 3);
    put_u32(m + o + 12, 12);
    m[o + 16] = 'x';
    m[o + 17] = 'd';
    m[o + 18] = 'g';
    m[o + 19] = '_';
    m[o + 20] = 'w';
    m[o + 21] = 'm';
    m[o + 22] = '_';
    m[o + 23] = 'b';
    m[o + 24] = 'a';
    m[o + 25] = 's';
    m[o + 26] = 'e';
    m[o + 27] = 0;
    put_u32(m + o + 28, 1);
    put_u32(m + o + 32, 8);
    o += n;
    /* wl_registry.bind(name=2, "wl_shm", ver=1, id=4) */
    n = 32;
    wl_put_hdr(m + o, 2, 0, n);
    put_u32(m + o + 8, 2);
    put_u32(m + o + 12, 7);
    m[o + 16] = 'w';
    m[o + 17] = 'l';
    m[o + 18] = '_';
    m[o + 19] = 's';
    m[o + 20] = 'h';
    m[o + 21] = 'm';
    m[o + 22] = 0;
    m[o + 23] = 0;
    put_u32(m + o + 24, 1);
    put_u32(m + o + 28, 4);
    o += n;
    /* wl_compositor.create_surface(new_id=5) desk */
    n = 12;
    wl_put_hdr(m + o, 3, 0, n);
    put_u32(m + o + 8, 5);
    o += n;
    /* xdg_wm_base.get_xdg_surface(new_id=9, surface=5) */
    n = 16;
    wl_put_hdr(m + o, 8, 2, n);
    put_u32(m + o + 8, 9);
    put_u32(m + o + 12, 5);
    o += n;
    /* xdg_surface.get_toplevel(new_id=10) */
    n = 12;
    wl_put_hdr(m + o, 9, 1, n);
    put_u32(m + o + 8, 10);
    o += n;
    /* xdg_toplevel.set_title("bar") */
    n = 16;
    wl_put_hdr(m + o, 10, 2, n);
    put_u32(m + o + 8, 4);
    m[o + 12] = 'b';
    m[o + 13] = 'a';
    m[o + 14] = 'r';
    m[o + 15] = 0;
    o += n;
    /* wl_shm.create_pool(new_id=6, size) — fd omitted (shared-AS vfork) */
    n = 16;
    wl_put_hdr(m + o, 4, 0, n);
    put_u32(m + o + 8, 6);
    put_u32(m + o + 12, psz);
    o += n;
    /* wl_shm_pool.create_buffer(id=7, off 0, desk) */
    n = 32;
    wl_put_hdr(m + o, 6, 0, n);
    put_u32(m + o + 8, 7);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, dw);
    put_u32(m + o + 20, dh);
    put_u32(m + o + 24, dw * 4U);
    put_u32(m + o + 28, WL_SHM_FORMAT_XRGB8888);
    o += n;
    /* xdg_surface.ack_configure(serial=1) */
    n = 12;
    wl_put_hdr(m + o, 9, 4, n);
    put_u32(m + o + 8, 1);
    o += n;
    /* wl_surface.attach(buffer=7) desk */
    n = 20;
    wl_put_hdr(m + o, 5, 1, n);
    put_u32(m + o + 8, 7);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, 0);
    o += n;
    /* wl_compositor.create_surface(new_id=11) app */
    n = 12;
    wl_put_hdr(m + o, 3, 0, n);
    put_u32(m + o + 8, 11);
    o += n;
    /* xdg_wm_base.get_xdg_surface(new_id=12, surface=11) */
    n = 16;
    wl_put_hdr(m + o, 8, 2, n);
    put_u32(m + o + 8, 12);
    put_u32(m + o + 12, 11);
    o += n;
    /* xdg_surface.get_toplevel(new_id=13) */
    n = 12;
    wl_put_hdr(m + o, 12, 1, n);
    put_u32(m + o + 8, 13);
    o += n;
#ifdef WL_QT_CLIENT
    /* xdg_toplevel.set_title("qt") */
    n = 16;
    wl_put_hdr(m + o, 13, 2, n);
    put_u32(m + o + 8, 3);
    m[o + 12] = 'q';
    m[o + 13] = 't';
    m[o + 14] = 0;
    m[o + 15] = 0;
    o += n;
#else
    /* xdg_toplevel.set_title("xdg") */
    n = 16;
    wl_put_hdr(m + o, 13, 2, n);
    put_u32(m + o + 8, 4);
    m[o + 12] = 'x';
    m[o + 13] = 'd';
    m[o + 14] = 'g';
    m[o + 15] = 0;
    o += n;
#endif
    /* wl_shm_pool.create_buffer(id=14, off poff, app) */
    n = 32;
    wl_put_hdr(m + o, 6, 0, n);
    put_u32(m + o + 8, 14);
    put_u32(m + o + 12, poff);
    put_u32(m + o + 16, aw);
    put_u32(m + o + 20, ah);
    put_u32(m + o + 24, aw * 4U);
    put_u32(m + o + 28, WL_SHM_FORMAT_XRGB8888);
    o += n;
    /* xdg_surface.ack_configure app */
    n = 12;
    wl_put_hdr(m + o, 12, 4, n);
    put_u32(m + o + 8, 1);
    o += n;
    /* wl_surface.attach(buffer=14) app */
    n = 20;
    wl_put_hdr(m + o, 11, 1, n);
    put_u32(m + o + 8, 14);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, 0);
    o += n;
    /* wl_surface.commit desk then app */
    n = 8;
    wl_put_hdr(m + o, 5, 6, n);
    o += n;
    n = 8;
    wl_put_hdr(m + o, 11, 6, n);
    o += n;
    return o;
}

/* Kernel bind/connect read sun_path from sockaddr+2. No filesystem node. */
static unsigned wl_unix_addr(unsigned char *raw)
{
    static const char path[] = "/tmp/wayland-0";
    unsigned i = 0;
    raw[0] = (unsigned char)AF_UNIX;
    raw[1] = 0;
    while (path[i] != 0) {
        raw[2U + i] = (unsigned char)path[i];
        i++;
    }
    raw[2U + i] = 0;
    return 2U + i + 1U;
}

static long wl_listen_unix(void)
{
    unsigned char addr[32];
    unsigned int alen;
    long fd;
    long rc;
    alen = wl_unix_addr(addr);
    fd = sys6(SYS_SOCKET, AF_UNIX, SOCK_STREAM, 0, 0, 0, 0);
    serial_hex("wl socket=", fd);
    if (fd < 0) {
        return fd;
    }
    rc = sys6(SYS_BIND, fd, (long)(unsigned long)addr, (long)alen, 0, 0, 0);
    serial_hex("wl bind=", rc);
    if (rc != 0) {
        return rc;
    }
    rc = sys6(SYS_LISTEN, fd, 4, 0, 0, 0, 0);
    serial_hex("wl listen=", rc);
    if (rc != 0) {
        return rc;
    }
    serial("[wl] listen ok\n", 15);
    return fd;
}

static long wl_connect_unix(void)
{
    unsigned char addr[32];
    unsigned int alen;
    long fd;
    long rc;
    alen = wl_unix_addr(addr);
    fd = sys6(SYS_SOCKET, AF_UNIX, SOCK_STREAM, 0, 0, 0, 0);
    serial_hex("wl cli sock=", fd);
    if (fd < 0) {
        return fd;
    }
    rc = sys6(SYS_CONNECT, fd, (long)(unsigned long)addr, (long)alen, 0, 0, 0);
    serial_hex("wl connect=", rc);
    if (rc != 0) {
        return rc;
    }
    return fd;
}

static long wl_write_all(long fd, const unsigned char *p, unsigned n)
{
    unsigned off = 0;
    while (off < n) {
        unsigned chunk = n - off;
        long w;
        if (chunk > 4096U) {
            chunk = 4096U;
        }
        w = sys6(SYS_WRITE, fd, (long)(unsigned long)(p + off), (long)chunk, 0, 0, 0);
        if (w <= 0) {
            return w < 0 ? w : -1;
        }
        off += (unsigned)w;
    }
    return (long)n;
}

static long wl_read_all(long fd, unsigned char *p, unsigned n)
{
    unsigned off = 0;
    while (off < n) {
        unsigned chunk = n - off;
        long r;
        if (chunk > 4096U) {
            chunk = 4096U;
        }
        r = sys6(SYS_READ, fd, (long)(unsigned long)(p + off), (long)chunk, 0, 0, 0);
        if (r <= 0) {
            return r < 0 ? r : (long)off;
        }
        off += (unsigned)r;
    }
    return (long)n;
}

static void paint_client_pool(unsigned char *pool, unsigned int dw, unsigned int dh,
                              unsigned int aw, unsigned int ah)
{
    draw_desk_chrome((unsigned int *)(void *)pool, dw, dh);
    draw_xdg_window((unsigned int *)(void *)(pool + dw * dh * 4U), aw, ah);
}

#ifndef WL_AS_CLIENT
static void paint_desk_only(unsigned char *pool, unsigned int dw, unsigned int dh)
{
    draw_desk_chrome((unsigned int *)(void *)pool, dw, dh);
}
#endif

static void wl_shm_path(char *p, unsigned i)
{
    p[0] = '/';
    p[1] = 't';
    p[2] = 'm';
    p[3] = 'p';
    p[4] = '/';
    p[5] = 'w';
    p[6] = 'l';
    p[7] = (char)('0' + (i / 10U) % 10U);
    p[8] = (char)('0' + (i % 10U));
    p[9] = 0;
}

static unsigned wl_shm_tiles(unsigned app_bytes)
{
    return (app_bytes + (unsigned)WL_TILE_BYTES - 1U) / (unsigned)WL_TILE_BYTES;
}

#ifdef WL_AS_CLIENT
static long wl_shm_put(const unsigned char *app, unsigned app_bytes)
{
    static const char magp[] = "/tmp/wlm";
    static const char mag[] = "SHM1";
    char path[12];
    unsigned i;
    unsigned tiles;
    long fd;
    long w;
    long magfd;
    unsigned total = 0;

    if (app_bytes > WL_APP_MAX_BYTES) {
        app_bytes = WL_APP_MAX_BYTES;
    }
    magfd = sys6(SYS_OPEN, (long)(unsigned long)magp, O_RDWR | O_CREAT | O_TRUNC, 420, 0, 0, 0);
    if (magfd >= 0) {
        (void)sys6(SYS_WRITE, magfd, (long)(unsigned long)mag, 4, 0, 0, 0);
        (void)sys6(SYS_CLOSE, magfd, 0, 0, 0, 0, 0);
    }
    tiles = wl_shm_tiles(app_bytes);
    for (i = 0; i < tiles; i++) {
        unsigned off = 0;
        unsigned chunk = app_bytes - i * (unsigned)WL_TILE_BYTES;
        if (chunk > (unsigned)WL_TILE_BYTES) {
            chunk = (unsigned)WL_TILE_BYTES;
        }
        wl_shm_path(path, i);
        fd = sys6(SYS_OPEN, (long)(unsigned long)path, O_RDWR | O_CREAT | O_TRUNC, 420, 0, 0, 0);
        if (fd < 0) {
            serial_hex("wl shm put open=", fd);
            return fd;
        }
        (void)sys6(SYS_FTRUNCATE, fd, (long)WL_TILE_BYTES, 0, 0, 0, 0);
        while (off < chunk) {
            unsigned c = chunk - off;
            if (c > 4096U) {
                c = 4096U;
            }
            w = sys6(SYS_WRITE, fd, (long)(unsigned long)(app + i * (unsigned)WL_TILE_BYTES + off),
                     (long)c, 0, 0, 0);
            if (w <= 0) {
                serial_hex("wl shm put wr=", w);
                (void)sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                return w < 0 ? w : -1;
            }
            off += (unsigned)w;
        }
        (void)sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        total += off;
    }
    return (long)total;
}
#endif

#ifndef WL_AS_CLIENT
static long wl_shm_get(unsigned char *app, unsigned app_bytes)
{
    static const char magp[] = "/tmp/wlm";
    char path[12];
    char mag[4];
    unsigned i;
    unsigned tiles;
    long fd;
    long r;
    long magfd;
    unsigned total = 0;

    if (app_bytes > WL_APP_MAX_BYTES) {
        app_bytes = WL_APP_MAX_BYTES;
    }
    magfd = sys6(SYS_OPEN, (long)(unsigned long)magp, O_RDWR, 0, 0, 0, 0);
    if (magfd < 0) {
        return magfd;
    }
    r = sys6(SYS_READ, magfd, (long)(unsigned long)mag, 4, 0, 0, 0);
    (void)sys6(SYS_CLOSE, magfd, 0, 0, 0, 0, 0);
    if (r != 4 || mag[0] != 'S' || mag[1] != 'H' || mag[2] != 'M' || mag[3] != '1') {
        serial("[wl] shm magic miss\n", 20);
        return -1;
    }
    tiles = wl_shm_tiles(app_bytes);
    for (i = 0; i < tiles; i++) {
        unsigned off = 0;
        unsigned chunk = app_bytes - i * (unsigned)WL_TILE_BYTES;
        if (chunk > (unsigned)WL_TILE_BYTES) {
            chunk = (unsigned)WL_TILE_BYTES;
        }
        wl_shm_path(path, i);
        fd = sys6(SYS_OPEN, (long)(unsigned long)path, O_RDWR, 0, 0, 0, 0);
        if (fd < 0) {
            serial_hex("wl shm get open=", fd);
            return fd;
        }
        while (off < chunk) {
            unsigned c = chunk - off;
            if (c > 4096U) {
                c = 4096U;
            }
            r = sys6(SYS_READ, fd, (long)(unsigned long)(app + i * (unsigned)WL_TILE_BYTES + off),
                     (long)c, 0, 0, 0);
            if (r <= 0) {
                serial_hex("wl shm get rd=", r);
                (void)sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
                return r < 0 ? r : (long)total;
            }
            off += (unsigned)r;
        }
        (void)sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        total += off;
    }
    return (long)total;
}
#endif

#ifdef WL_STUB_LIB
/* Linked into qt_wl_hello.elf (real QGuiApplication). Same tiles + wire as
 * the C p8test client. Desk chrome stays compositor-side. */
long wl_stub_flush_app(const unsigned char *app, unsigned w, unsigned h)
{
    static const char shmm[] = "[qt] QGuiApplication shm\n";
    static const char wirem[] = "[qt] QGuiApplication wire\n";
    unsigned int desk_w = 1024;
    unsigned int desk_h = 768;
    unsigned int app_w;
    unsigned int app_h;
    unsigned int app_bytes;
    unsigned char msg[512];
    unsigned char hdr[4];
    unsigned int msglen;
    long cli_fd;
    long putn;

    app_w = w;
    app_h = h;
    if (app_w == 0 || app_h == 0) {
        app_w = 480;
        app_h = 320;
    }
    if (app_w > 480U) {
        app_w = 480;
    }
    if (app_h > 320U) {
        app_h = 320;
    }
    app_bytes = app_w * app_h * 4U;
    putn = wl_shm_put(app, app_bytes);
    serial_hex("wl shm put=", putn);
    if (putn > 0) {
        serial(shmm, sizeof(shmm) - 1);
    }
    cli_fd = wl_connect_unix();
    msglen = wl_client_build(msg, desk_w, desk_h, app_w, app_h);
    if (cli_fd >= 0) {
        put_u32(hdr, msglen);
        (void)wl_write_all(cli_fd, hdr, 4);
        (void)wl_write_all(cli_fd, msg, msglen);
        serial(wirem, sizeof(wirem) - 1);
    }
    return putn;
}
#else
#ifdef WL_AS_CLIENT
void _start(void)
{
#ifdef WL_QT_CLIENT
    static const char hello[] = "[qt] p8test.elf wayland client\n";
    static const char qpa[] = "[qt] QT_QPA_PLATFORM=wayland\n";
    static const char paintm[] = "[qt] p8test.elf paint\n";
    static const char shmm[] = "[qt] p8test.elf shm\n";
    static const char wirem[] = "[qt] p8test.elf wire\n";
#else
    static const char hello[] = "[wl] hello.elf client\n";
    static const char paintm[] = "[wl] hello.elf paint\n";
    static const char shmm[] = "[wl] hello.elf shm\n";
    static const char wirem[] = "[wl] hello.elf wire\n";
#endif
    unsigned int desk_w = 1024;
    unsigned int desk_h = 768;
    unsigned int app_w = 480;
    unsigned int app_h = 320;
    unsigned int pool_bytes;
    long mapped;
    long cli_fd;
    unsigned char msg[512];
    unsigned char hdr[4];
    unsigned int msglen;
    unsigned char *pool;

    serial(hello, sizeof(hello) - 1);
#ifdef WL_QT_CLIENT
    serial(qpa, sizeof(qpa) - 1);
#endif
    pool_bytes = desk_w * desk_h * 4U + app_w * app_h * 4U;
    mapped = sys6(9, 0, (long)pool_bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    serial_hex("wl cli mmap=", mapped);
    if (mapped < 0x1000) {
        (void)sys6(SYS_EXIT, 1, 0, 0, 0, 0, 0);
        for (;;) {
        }
    }
    pool = (unsigned char *)(unsigned long)mapped;
    serial_hex("wl cli w=", (long)desk_w);
    serial_hex("wl cli h=", (long)desk_h);
    paint_client_pool(pool, desk_w, desk_h, app_w, app_h);
    serial(paintm, sizeof(paintm) - 1);
    {
        unsigned int app_bytes = app_w * app_h * 4U;
        long putn = wl_shm_put(pool + desk_w * desk_h * 4U, app_bytes);
        serial_hex("wl shm put=", putn);
        if (putn > 0) {
            serial(shmm, sizeof(shmm) - 1);
        }
    }
    cli_fd = wl_connect_unix();
    msglen = wl_client_build(msg, desk_w, desk_h, app_w, app_h);
    if (cli_fd >= 0) {
        put_u32(hdr, msglen);
        (void)wl_write_all(cli_fd, hdr, 4);
        (void)wl_write_all(cli_fd, msg, msglen);
        serial(wirem, sizeof(wirem) - 1);
    }
    (void)sys6(SYS_EXIT, 0, 0, 0, 0, 0, 0);
    for (;;) {
    }
}
#else
void _start(void)
{
    static const char hello[] = "[compositor] guest stub hello\n";
    static const char fillok[] = "[compositor] guest stub fb fill\n";
    static const char wlok[] = "[compositor] wayland native desk blit\n";
    static const char xdgok[] = "[compositor] xdg-shell window\n";
    static const char childm[] = "[wl] fork child\n";
    static const char parentm[] = "[wl] fork parent\n";
    static const char vchildm[] = "[wl] vfork child\n";
    static const char vparentm[] = "[wl] vfork parent\n";
    static const char fallback[] = "[wl] unix fallback in-process\n";
    static const char deskm[] = "[wl] xdg client paint\n";
    static const char accepm[] = "[wl] client accepted\n";
    static const char shmok[] = "[wl] client shm blit\n";
    static char p8_path[] = "/p8test.elf";
    static char hello_path[] = "/hello.elf";
    static char *p8_argv[2];
    static char *hello_argv[2];
    static char env_qpa[] = "QT_QPA_PLATFORM=wayland";
    static char env_wld[] = "WAYLAND_DISPLAY=wayland-0";
    static char env_xdg[] = "XDG_RUNTIME_DIR=/tmp";
    static char *p8_envp[4];
    struct fbinfo info;
    struct wl_state st;
    unsigned int desk_w;
    unsigned int desk_h;
    unsigned int app_w;
    unsigned int app_h;
    unsigned i;
    long mapped;
    unsigned char msg[512];
    unsigned int msglen;
    unsigned int pool_bytes;
    long listen_fd;
    long cli_fd;
    long acc_fd;
    long pid;
    long nread;
    unsigned int map_bytes;
    long exec_rc;
    int used_vfork;

    serial(hello, sizeof(hello) - 1);

    info.addr = 0;
    info.pitch = 0;
    info.width = 0;
    info.height = 0;
    info.bpp = 0;
    info.pad[0] = info.pad[1] = info.pad[2] = 0;
    info.ready = 0;
    (void)sys6(1001, (long)&info, 0, 0, 0, 0, 0);
    /* APP role: Linux mmap is nr 9. Native 26 is msync here. */
    mapped = sys6(9, 0, 0, 3, 1, BFREE_FB0_FD, 0);
    serial_hex("fb mmap=", mapped);
    if (mapped < 0 || info.ready == 0 || info.width == 0 || info.height == 0 ||
        info.pitch == 0) {
        for (;;) {
        }
    }

    st.fb = (unsigned char *)mapped;
    st.fb_pitch = info.pitch;
    st.fb_w = info.width;
    st.fb_h = info.height;
    st.pool = 0;
    st.pool_size = 0;
    st.xdg = 0;
    for (i = 0; i < WL_VIEW_N; i++) {
        st.view[i].buf_off = 0;
        st.view[i].buf_w = 0;
        st.view[i].buf_h = 0;
        st.view[i].buf_stride = 0;
        st.view[i].buf_format = 0;
        st.view[i].attached = 0;
        st.view[i].committed = 0;
        st.view[i].x = 0;
        st.view[i].y = 0;
    }
    st.view[1].x = 540;
    st.view[1].y = 48;

    /* Desk wallpaper color — not magenta. Magenta under the desk flashes
     * through full-FB present / cursor restore. Proof-of-life fill is over. */
    fill_rect(st.fb, st.fb_pitch, st.fb_w, st.fb_h, 0, 0, st.fb_w, st.fb_h, 0x007A8FA8UL);
    serial(fillok, sizeof(fillok) - 1);

    desk_w = st.fb_w;
    desk_h = st.fb_h;
    if (desk_w > WL_SURF_MAX_W) {
        desk_w = WL_SURF_MAX_W;
    }
    if (desk_h > WL_SURF_MAX_H) {
        desk_h = WL_SURF_MAX_H;
    }
    app_w = 480;
    app_h = 320;
    pool_bytes = desk_w * desk_h * 4U + app_w * app_h * 4U;
    map_bytes =
        (unsigned int)WL_SURF_MAX_W * (unsigned int)WL_SURF_MAX_H * 4U + 480U * 320U * 4U;
    {
        long shm_map = sys6(9, 0, (long)map_bytes, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        serial_hex("wl shm mmap=", shm_map);
        if (shm_map < 0x1000) {
            for (;;) {
            }
        }
        st.pool = (unsigned char *)(unsigned long)shm_map;
    }
    st.pool_size = pool_bytes;
    serial_hex("wl desk w=", (long)desk_w);
    serial_hex("wl desk h=", (long)desk_h);

    listen_fd = wl_listen_unix();
    cli_fd = -1;
    acc_fd = -1;
    pid = -1;
    used_vfork = 0;
    if (listen_fd >= 0) {
        pid = sys6(SYS_FORK, 0, 0, 0, 0, 0, 0);
        serial_hex("wl fork=", pid);
        if (pid < 0) {
            used_vfork = 1;
            pid = sys6(SYS_VFORK, 0, 0, 0, 0, 0, 0);
            serial_hex("wl vfork=", pid);
        }
    }

    if (pid == 0) {
        unsigned char hdr[4];
        serial(used_vfork ? vchildm : childm,
               used_vfork ? (sizeof(vchildm) - 1) : (sizeof(childm) - 1));
        p8_argv[0] = p8_path;
        p8_argv[1] = 0;
        p8_envp[0] = env_qpa;
        p8_envp[1] = env_wld;
        p8_envp[2] = env_xdg;
        p8_envp[3] = 0;
        serial("[wl] execve p8test.elf\n", 23);
        exec_rc = sys6(SYS_EXECVE, (long)(unsigned long)p8_path,
                       (long)(unsigned long)p8_argv, (long)(unsigned long)p8_envp,
                       0, 0, 0);
        serial_hex("wl execve p8test=", exec_rc);
        serial("[wl] execve hello.elf\n", 22);
        hello_argv[0] = hello_path;
        hello_argv[1] = 0;
        exec_rc = sys6(SYS_EXECVE, (long)(unsigned long)hello_path,
                       (long)(unsigned long)hello_argv, 0, 0, 0, 0);
        serial_hex("wl execve hello=", exec_rc);
        paint_client_pool(st.pool, desk_w, desk_h, app_w, app_h);
        serial(deskm, sizeof(deskm) - 1);
        cli_fd = wl_connect_unix();
        msglen = wl_client_build(msg, desk_w, desk_h, app_w, app_h);
        if (cli_fd >= 0) {
            put_u32(hdr, msglen);
            (void)wl_write_all(cli_fd, hdr, 4);
            (void)wl_write_all(cli_fd, msg, msglen);
            put_u32(hdr, pool_bytes);
            (void)wl_write_all(cli_fd, hdr, 4);
            (void)wl_write_all(cli_fd, st.pool, pool_bytes);
        }
        (void)sys6(SYS_EXIT, 0, 0, 0, 0, 0, 0);
        for (;;) {
        }
    }

    if (pid > 0 && listen_fd >= 0) {
        unsigned char hdr[4];
        unsigned int wlen;
        serial(used_vfork ? vparentm : parentm,
               used_vfork ? (sizeof(vparentm) - 1) : (sizeof(parentm) - 1));
        acc_fd = sys6(SYS_ACCEPT, listen_fd, 0, 0, 0, 0, 0);
        serial_hex("wl accept=", acc_fd);
        if (acc_fd >= 0) {
            serial(accepm, sizeof(accepm) - 1);
            nread = wl_read_all(acc_fd, hdr, 4);
            wlen = (nread == 4) ? get_u32(hdr) : 0;
            if (wlen > 0 && wlen <= 512) {
                nread = wl_read_all(acc_fd, msg, wlen);
                serial_hex("wl bytes=", nread);
                paint_desk_only(st.pool, desk_w, desk_h);
                {
                    unsigned int app_bytes = app_w * app_h * 4U;
                    long got = wl_shm_get(st.pool + desk_w * desk_h * 4U, app_bytes);
                    serial_hex("wl shm get=", got);
                    (void)sys6(SYS_WAITPID, pid, (long)(unsigned long)&g_waitst, WNOHANG, 0, 0,
                               0);
                    if (got == (long)app_bytes) {
                        serial(shmok, sizeof(shmok) - 1);
                    } else {
                        draw_xdg_window((unsigned int *)(void *)(st.pool + desk_w * desk_h * 4U),
                                        app_w, app_h);
                    }
                }
                if (nread > 0) {
                    wl_dispatch(&st, msg, (unsigned int)nread);
                }
            }
        }
    }

    if (!wl_committed(&st)) {
        serial(fallback, sizeof(fallback) - 1);
        paint_client_pool(st.pool, desk_w, desk_h, app_w, app_h);
        serial(deskm, sizeof(deskm) - 1);
        if (listen_fd >= 0) {
            cli_fd = wl_connect_unix();
            acc_fd = sys6(SYS_ACCEPT, listen_fd, 0, 0, 0, 0, 0);
            serial_hex("wl accept=", acc_fd);
            msglen = wl_client_build(msg, desk_w, desk_h, app_w, app_h);
            if (cli_fd >= 0) {
                (void)sys6(SYS_WRITE, cli_fd, (long)(unsigned long)msg, (long)msglen, 0, 0, 0);
            }
            if (acc_fd >= 0) {
                serial(accepm, sizeof(accepm) - 1);
                nread = sys6(SYS_READ, acc_fd, (long)(unsigned long)msg, 512, 0, 0, 0);
                serial_hex("wl bytes=", nread);
                if (nread > 0) {
                    wl_dispatch(&st, msg, (unsigned int)nread);
                }
            }
        }
        if (!wl_committed(&st)) {
            msglen = wl_client_build(msg, desk_w, desk_h, app_w, app_h);
            serial_hex("wl bytes=", (long)msglen);
            wl_dispatch(&st, msg, msglen);
        }
    }
    if (wl_committed(&st)) {
        serial(wlok, sizeof(wlok) - 1);
        if (st.xdg) {
            serial(xdgok, sizeof(xdgok) - 1);
            blit_shm(&st);
        }
    }
    cursor_loop(&st);
}
#endif /* WL_AS_CLIENT */
#endif /* WL_STUB_LIB */
