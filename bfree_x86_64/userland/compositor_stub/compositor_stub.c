/* Freestanding guest compositor. Reached via init_tramp exec_initrd (APP role).
 * Parent = server, vfork child = client. Pipe carries Wayland requests.
 * Shared AS (vfork) so the child's wl_shm pixels are visible to the parent.
 * Not Linux tron_gui_server. Do not set BFREE_BOOT_GUI_FIRST on the daily kernel.
 */
#define BFREE_FB0_FD 0x2000
#define WL_SHM_FORMAT_XRGB8888 1
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define PROT_READ 1
#define PROT_WRITE 2
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

/* Client-side desk chrome. Not product DesktopShell.qml. */
static void draw_desk_chrome(unsigned int *p, unsigned int w, unsigned int h)
{
    static const unsigned int icons[15] = {
        0x002E6BFFUL, 0x0028A745UL, 0x00E07A2EUL, 0x007B2CBFUL, 0x0020C997UL,
        0x000DCAF0UL, 0x001F4E79UL, 0x006F42C1UL, 0x000D6EFDUL, 0x00198754UL,
        0x000DCAF0UL, 0x00DC3545UL, 0x00198754UL, 0x00E07A2EUL, 0x006F42C1UL
    };
    unsigned int i;
    unsigned int bar = (h > 48U) ? 48U : (h / 6U);
    unsigned int iy;
    unsigned int ix;
    unsigned int gapx;
    unsigned int gapy;
    pool_rect(p, w, h, 0, 0, w, h, 0x0094A3B8UL);
    pool_rect(p, w, h, 0, h - bar, w, bar, 0x00101828UL);
    pool_rect(p, w, h, 8, h - bar + 8, 72, bar > 16U ? bar - 16U : bar, 0x003D5C9EUL);
    pool_rect(p, w, h, 88, h - bar + 12, 160, bar > 24U ? bar - 24U : 8U, 0x00202838UL);
    if (w > 96U) {
        pool_rect(p, w, h, w - 88, h - bar + 12, 72, bar > 24U ? bar - 24U : 8U, 0x00202838UL);
    }
    gapx = (w >= 800U) ? 140U : 76U;
    gapy = (h >= 600U) ? 110U : 76U;
    for (i = 0; i < 15U; i++) {
        ix = 36U + (i % 6U) * gapx;
        iy = 36U + (i / 6U) * gapy;
        if (ix + 56U < w && iy + 56U + bar < h) {
            pool_rect(p, w, h, ix, iy, 56, 56, icons[i]);
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

void _start(void)
{
    static const char hello[] = "[compositor] guest stub hello\n";
    static const char fillok[] = "[compositor] guest stub fb fill\n";
    static const char wlok[] = "[compositor] wayland desk chrome blit\n";
    static const char childm[] = "[wl] vfork child\n";
    static const char parentm[] = "[wl] vfork parent\n";
    static const char fallback[] = "[wl] vfork fallback in-process\n";
    static const char deskm[] = "[wl] desk chrome\n";
    struct fbinfo info;
    struct wl_state st;
    unsigned int surf_w;
    unsigned int surf_h;
    long mapped;
    long shm_map;
    unsigned char msg[256];
    unsigned int msglen;
    unsigned int pool_bytes;
    int fds[2];
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

    fds[0] = fds[1] = -1;
    if (sys6(22, (long)fds, 0, 0, 0, 0, 0) != 0) {
        fds[0] = fds[1] = -1;
    }
    pid = sys6(58, 0, 0, 0, 0, 0, 0);
    serial_hex("wl vfork=", pid);

    if (pid == 0) {
        serial(childm, sizeof(childm) - 1);
        draw_desk_chrome((unsigned int *)(void *)st.pool, surf_w, surf_h);
        serial(deskm, sizeof(deskm) - 1);
        msglen = wl_client_build(msg, surf_w, surf_h);
        if (fds[1] >= 0) {
            (void)sys6(1, (long)fds[1], (long)msg, (long)msglen, 0, 0, 0);
        }
        (void)sys6(60, 0, 0, 0, 0, 0, 0);
        for (;;) {
        }
    }

    if (pid > 0 && fds[0] >= 0) {
        serial(parentm, sizeof(parentm) - 1);
        nread = sys6(0, (long)fds[0], (long)msg, 256, 0, 0, 0);
        serial_hex("wl bytes=", nread);
        if (nread > 0) {
            wl_dispatch(&st, msg, (unsigned int)nread);
        }
    } else {
        serial(fallback, sizeof(fallback) - 1);
        draw_desk_chrome((unsigned int *)(void *)st.pool, surf_w, surf_h);
        serial(deskm, sizeof(deskm) - 1);
        msglen = wl_client_build(msg, surf_w, surf_h);
        serial_hex("wl bytes=", (long)msglen);
        wl_dispatch(&st, msg, msglen);
    }
    if (st.committed) {
        serial(wlok, sizeof(wlok) - 1);
    }
    for (;;) {
    }
}
