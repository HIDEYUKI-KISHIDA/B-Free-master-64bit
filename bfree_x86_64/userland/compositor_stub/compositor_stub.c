/* Freestanding guest compositor. In-process Wayland wire + wl_shm blit.
 * Not Linux tron_gui_server. Not a second client ELF (PID1 cannot fork here).
 * Do not set BFREE_BOOT_GUI_FIRST on the daily kernel.
 */
#define BFREE_FB0_FD 0x2000
#define WL_SHM_FORMAT_XRGB8888 1
#define WL_SURF_W 480
#define WL_SURF_H 320

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
    x0 = (st->fb_w > st->buf_w) ? (st->fb_w - st->buf_w) / 2U : 0;
    y0 = (st->fb_h > st->buf_h) ? (st->fb_h - st->buf_h) / 2U : 0;
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

static unsigned wl_client_build(unsigned char *m)
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
    /* wl_shm.create_pool(new_id=6, size) — fd omitted (same-process mmap) */
    n = 16;
    wl_put_hdr(m + o, 4, 0, n);
    put_u32(m + o + 8, 6);
    put_u32(m + o + 12, (unsigned int)(WL_SURF_W * WL_SURF_H * 4));
    o += n;
    /* wl_shm_pool.create_buffer(id=7, off, w, h, stride, XRGB8888) */
    n = 32;
    wl_put_hdr(m + o, 6, 0, n);
    put_u32(m + o + 8, 7);
    put_u32(m + o + 12, 0);
    put_u32(m + o + 16, WL_SURF_W);
    put_u32(m + o + 20, WL_SURF_H);
    put_u32(m + o + 24, WL_SURF_W * 4);
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

void _start(void)
{
    static const char hello[] = "[compositor] guest stub hello\n";
    static const char fillok[] = "[compositor] guest stub fb fill\n";
    static const char wlok[] = "[compositor] wayland shm blit\n";
    struct fbinfo info;
    struct wl_state st;
    static unsigned int shm_pool[WL_SURF_W * WL_SURF_H];
    long mapped;
    unsigned char msg[256];
    unsigned int msglen;
    unsigned int i;
    unsigned int pool_bytes;

    serial(hello, sizeof(hello) - 1);

    info.addr = 0;
    info.pitch = 0;
    info.width = 0;
    info.height = 0;
    info.bpp = 0;
    info.pad[0] = info.pad[1] = info.pad[2] = 0;
    info.ready = 0;
    (void)sys6(1001, (long)&info, 0, 0, 0, 0, 0);
    mapped = sys6(26, 0, 0, 3, 1, BFREE_FB0_FD, 0);
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

    /* Do not anonymous-mmap the pool: INIT PID1 heap mmap was the hang
     * after magenta (no [wl] lines). ELF BSS is mapped via p_memsz. */
    pool_bytes = (unsigned int)(WL_SURF_W * WL_SURF_H * 4);
    st.pool = (unsigned char *)(void *)shm_pool;
    st.pool_size = pool_bytes;
    serial_hex("wl shm static=", (long)(unsigned long)st.pool);
    for (i = 0; i < (pool_bytes / 4U); i++) {
        unsigned int x = i % WL_SURF_W;
        unsigned int y = i / WL_SURF_W;
        unsigned int edge = (x < 8U || y < 8U || x >= (WL_SURF_W - 8U) || y >= (WL_SURF_H - 8U));
        shm_pool[i] = edge ? 0x00FFFFFFUL : 0x0000FFFFUL;
    }

    msglen = wl_client_build(msg);
    serial_hex("wl bytes=", (long)msglen);
    wl_dispatch(&st, msg, msglen);
    if (st.committed) {
        serial(wlok, sizeof(wlok) - 1);
    }
    for (;;) {
    }
}
