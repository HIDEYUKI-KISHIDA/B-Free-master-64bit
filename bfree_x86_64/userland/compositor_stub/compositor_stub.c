/* Freestanding guest compositor. Reached via init_tramp exec_initrd (APP role).
 * APP Linux sockets: 41 socket, 49 bind, 50 listen, 42 connect, 43 accept.
 * Listen on AF_UNIX /tmp/wayland-0. vfork child connect()s and paints the
 * daily FB desk lookalike (EX/VW tiles) into wl_shm. After blit, APP polls
 * sys_poll_input_event (nr 0, BSS ptr) and draws an X11 left_ptr arrow on FB.
 * Not desktop.elf (g1-desk execve would load busybox; from-source kernel dies).
 * Do not drop QT_QPA_PLATFORM=bfree on daily bfree.iso. Do not GUI_FIRST.
 */
#define BFREE_FB0_FD 0x2000
#define WL_SHM_FORMAT_XRGB8888 1
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define PROT_READ 1
#define PROT_WRITE 2
#define AF_UNIX 1
#define SOCK_STREAM 1
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_SOCKET 41
#define SYS_CONNECT 42
#define SYS_ACCEPT 43
#define SYS_BIND 49
#define SYS_LISTEN 50
#define SYS_VFORK 58
#define SYS_EXIT 60
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

struct wl_state {
    unsigned char *fb;
    unsigned int fb_pitch;
    unsigned int fb_w;
    unsigned int fb_h;
    unsigned char *pool;
    unsigned int pool_size;
    unsigned int buf_off;
    unsigned int buf_w;
    unsigned int buf_h;
    unsigned int buf_stride;
    unsigned int buf_format;
    unsigned int attached;
    unsigned int committed;
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

static void blit_shm(struct wl_state *st)
{
    unsigned int x0;
    unsigned int y0;
    unsigned int y;
    unsigned int x;
    if (!st->fb || !st->pool || !st->attached || st->buf_format != WL_SHM_FORMAT_XRGB8888) {
        return;
    }
    if (st->buf_w == 0 || st->buf_h == 0 || st->buf_stride < st->buf_w * 4U) {
        return;
    }
    if (st->buf_off + st->buf_stride * st->buf_h > st->pool_size) {
        return;
    }
    x0 = 0;
    y0 = 0;
    for (y = 0; y < st->buf_h && (y0 + y) < st->fb_h; y++) {
        unsigned int *dst = (unsigned int *)(st->fb + (unsigned long)(y0 + y) * st->fb_pitch);
        unsigned int *src = (unsigned int *)(st->pool + st->buf_off + (unsigned long)y * st->buf_stride);
        unsigned int limit = st->buf_w;
        if (x0 + limit > st->fb_w) {
            limit = st->fb_w - x0;
        }
        for (x = 0; x < limit; x++) {
            dst[x0 + x] = src[x];
        }
    }
    st->committed = 1;
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
    cursor_place(st, g_mx, g_my);
    serial(curon, sizeof(curon) - 1);
    for (;;) {
        int n;
        for (n = 0; n < 32; n++) {
            g_poll.type = 0;
            g_poll.keycode = 0;
            g_poll.mouse_x = 0;
            g_poll.mouse_y = 0;
            g_poll.mouse_btn = 0;
            ret = sys6(0, (long)(unsigned long)&g_poll, 0, 0, 0, 0, 0);
            if (ret <= 0) {
                break;
            }
            if (g_poll.type == 3) {
                int mx = clamp_i(g_poll.mouse_x, 0, (int)st->fb_w - 1);
                int my = clamp_i(g_poll.mouse_y, 0, (int)st->fb_h - 1);
                if (!logged) {
                    serial_hex("wl mouse x=", (long)mx);
                    serial_hex("wl mouse y=", (long)my);
                    logged = 1;
                }
                if (mx != g_mx || my != g_my) {
                    g_mx = mx;
                    g_my = my;
                    cursor_place(st, g_mx, g_my);
                }
            }
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
            serial("[wl] bind\n", 10);
            serial_hex("wl bind name=", (long)get_u32(a));
        } else if (id == 3 && op == 0) {
            serial("[wl] create_surface\n", 21);
        } else if (id == 4 && op == 0) {
            st->pool_size = get_u32(a + 4);
            serial("[wl] create_pool\n", 18);
            serial_hex("wl pool size=", (long)st->pool_size);
        } else if (id == 6 && op == 0) {
            st->buf_off = get_u32(a + 4);
            st->buf_w = get_u32(a + 8);
            st->buf_h = get_u32(a + 12);
            st->buf_stride = get_u32(a + 16);
            st->buf_format = get_u32(a + 20);
            serial("[wl] create_buffer\n", 20);
            serial_hex("wl buf w=", (long)st->buf_w);
            serial_hex("wl buf h=", (long)st->buf_h);
        } else if (id == 5 && op == 1) {
            st->attached = (get_u32(a) != 0);
            serial("[wl] attach\n", 13);
        } else if (id == 5 && op == 6) {
            serial("[wl] commit\n", 13);
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

/* Daily guest_paint_fb_desktopshell lookalike on wl_shm. Not DesktopShell.qml. */
static void draw_desk_chrome(unsigned int *p, unsigned int w, unsigned int h)
{
    static const char *acro[16] = {
        "EX", "VW", "TE", "SM", "SS", "DI", "AS", "NO",
        "CL", "CA", "PB", "NM", "BA", "SO", "JI", "TR"
    };
    static const char *title[16] = {
        "Explorer", "Viewer", "Terminal", "SystemMonitor", "SystemSettings", "Discover",
        "AppStore", "Notification", "ClockApplet", "Calculator", "PaintBoard",
        "NetworkManager", "BatteryManager", "SoundManager", "JapaneseIME", "Trash"
    };
    static const unsigned int accent[16] = {
        0x001D4ED8UL, 0x000F766EUL, 0x00C2410CUL, 0x007C3AEDUL, 0x000F766EUL, 0x000D9488UL,
        0x001E3A8AUL, 0x007C3AEDUL, 0x00C2410CUL, 0x001D4ED8UL, 0x000D9488UL, 0x001D4ED8UL,
        0x000F766EUL, 0x00BE185DUL, 0x007C3AEDUL, 0x00475569UL
    };
    unsigned int bar = (h > 52U) ? 52U : (h / 6U);
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
        pool_rect(p, w, h, (unsigned)(x + 14), (unsigned)y, tile, tile, accent[i]);
        shm_text(p, w, h, x + 14 + (int)(tile - str_px(acro[i], 2)) / 2, y + 16, acro[i],
                 0x00F8FAFCUL, 2);
        {
            int tw = (int)str_px(title[i], 1);
            int tx = x + (76 - tw) / 2;
            if (tx < x) {
                tx = x;
            }
            shm_text(p, w, h, tx, y + 54, title[i], 0x00E8EEF5UL, 1);
        }
    }
}

static unsigned wl_client_build(unsigned char *m, unsigned int w, unsigned int h)
{
    unsigned int o = 0;
    unsigned int n;
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
    /* wl_compositor.create_surface(new_id=5) */
    n = 12;
    wl_put_hdr(m + o, 3, 0, n);
    put_u32(m + o + 8, 5);
    o += n;
    /* wl_shm.create_pool(new_id=6, size) — fd omitted (shared-AS vfork) */
    n = 16;
    wl_put_hdr(m + o, 4, 0, n);
    put_u32(m + o + 8, 6);
    put_u32(m + o + 12, w * h * 4U);
    o += n;
    /* wl_shm_pool.create_buffer(id=7, off, w, h, stride, XRGB8888) */
    n = 32;
    wl_put_hdr(m + o, 6, 0, n);
    put_u32(m + o + 8, 7);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, w);
    put_u32(m + o + 20, h);
    put_u32(m + o + 24, w * 4U);
    put_u32(m + o + 28, WL_SHM_FORMAT_XRGB8888);
    o += n;
    /* wl_surface.attach(buffer=7, x=0, y=0) */
    n = 20;
    wl_put_hdr(m + o, 5, 1, n);
    put_u32(m + o + 8, 7);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, 0);
    o += n;
    /* wl_surface.commit */
    n = 8;
    wl_put_hdr(m + o, 5, 6, n);
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
    rc = sys6(SYS_LISTEN, fd, 1, 0, 0, 0, 0);
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

void _start(void)
{
    static const char hello[] = "[compositor] guest stub hello\n";
    static const char fillok[] = "[compositor] guest stub fb fill\n";
    static const char wlok[] = "[compositor] wayland native desk blit\n";
    static const char childm[] = "[wl] vfork child\n";
    static const char parentm[] = "[wl] vfork parent\n";
    static const char fallback[] = "[wl] unix fallback in-process\n";
    static const char deskm[] = "[wl] desk chrome\n";
    static const char accepm[] = "[wl] client accepted\n";
    struct fbinfo info;
    struct wl_state st;
    unsigned int surf_w;
    unsigned int surf_h;
    long mapped;
    long shm_map;
    unsigned char msg[256];
    unsigned int msglen;
    unsigned int pool_bytes;
    long listen_fd;
    long cli_fd;
    long acc_fd;
    long pid;
    long nread;

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
    st.buf_off = 0;
    st.buf_w = 0;
    st.buf_h = 0;
    st.buf_stride = 0;
    st.buf_format = 0;
    st.attached = 0;
    st.committed = 0;

    fill_rect(st.fb, st.fb_pitch, st.fb_w, st.fb_h, 0, 0, st.fb_w, st.fb_h, 0x00FF00FFUL);
    serial(fillok, sizeof(fillok) - 1);

    surf_w = st.fb_w;
    surf_h = st.fb_h;
    if (surf_w > WL_SURF_MAX_W) {
        surf_w = WL_SURF_MAX_W;
    }
    if (surf_h > WL_SURF_MAX_H) {
        surf_h = WL_SURF_MAX_H;
    }
    pool_bytes = surf_w * surf_h * 4U;
    /* APP Linux mmap nr 9. Heap path already mapped 0x3c00000 for 480x320. */
    shm_map = sys6(9, 0, (long)pool_bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    serial_hex("wl shm mmap=", shm_map);
    if (shm_map < 0x1000) {
        for (;;) {
        }
    }
    st.pool = (unsigned char *)(unsigned long)shm_map;
    st.pool_size = pool_bytes;
    serial_hex("wl surf w=", (long)surf_w);
    serial_hex("wl surf h=", (long)surf_h);

    listen_fd = wl_listen_unix();
    cli_fd = -1;
    acc_fd = -1;
    pid = -1;
    if (listen_fd >= 0) {
        pid = sys6(SYS_VFORK, 0, 0, 0, 0, 0, 0);
        serial_hex("wl vfork=", pid);
    }

    if (pid == 0) {
        serial(childm, sizeof(childm) - 1);
        draw_desk_chrome((unsigned int *)(void *)st.pool, surf_w, surf_h);
        serial(deskm, sizeof(deskm) - 1);
        cli_fd = wl_connect_unix();
        msglen = wl_client_build(msg, surf_w, surf_h);
        if (cli_fd >= 0) {
            (void)sys6(SYS_WRITE, cli_fd, (long)(unsigned long)msg, (long)msglen, 0, 0, 0);
        }
        (void)sys6(SYS_EXIT, 0, 0, 0, 0, 0, 0);
        for (;;) {
        }
    }

    if (pid > 0 && listen_fd >= 0) {
        serial(parentm, sizeof(parentm) - 1);
        acc_fd = sys6(SYS_ACCEPT, listen_fd, 0, 0, 0, 0, 0);
        serial_hex("wl accept=", acc_fd);
        if (acc_fd >= 0) {
            serial(accepm, sizeof(accepm) - 1);
            nread = sys6(SYS_READ, acc_fd, (long)(unsigned long)msg, 256, 0, 0, 0);
            serial_hex("wl bytes=", nread);
            if (nread > 0) {
                wl_dispatch(&st, msg, (unsigned int)nread);
            }
        }
    }

    if (!st.committed) {
        serial(fallback, sizeof(fallback) - 1);
        draw_desk_chrome((unsigned int *)(void *)st.pool, surf_w, surf_h);
        serial(deskm, sizeof(deskm) - 1);
        if (listen_fd >= 0) {
            cli_fd = wl_connect_unix();
            acc_fd = sys6(SYS_ACCEPT, listen_fd, 0, 0, 0, 0, 0);
            serial_hex("wl accept=", acc_fd);
            msglen = wl_client_build(msg, surf_w, surf_h);
            if (cli_fd >= 0) {
                (void)sys6(SYS_WRITE, cli_fd, (long)(unsigned long)msg, (long)msglen, 0, 0, 0);
            }
            if (acc_fd >= 0) {
                serial(accepm, sizeof(accepm) - 1);
                nread = sys6(SYS_READ, acc_fd, (long)(unsigned long)msg, 256, 0, 0, 0);
                serial_hex("wl bytes=", nread);
                if (nread > 0) {
                    wl_dispatch(&st, msg, (unsigned int)nread);
                }
            }
        }
        if (!st.committed) {
            msglen = wl_client_build(msg, surf_w, surf_h);
            serial_hex("wl bytes=", (long)msglen);
            wl_dispatch(&st, msg, msglen);
        }
    }
    if (st.committed) {
        serial(wlok, sizeof(wlok) - 1);
    }
    cursor_loop(&st);
}
