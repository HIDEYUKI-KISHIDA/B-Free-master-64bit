static unsigned g_splash_frame = 0;
static int g_splash_fb_ready = 0;
static unsigned g_splash_fb_pitch = 4096u;
static unsigned g_splash_fb_w = 1024u;
static unsigned g_splash_fb_h = 768u;

typedef struct {
    void *addr;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned char bpp;
    int ready;
} splash_fbinfo_wire_t;

static void splash_put(unsigned char *fb, unsigned pitch, unsigned fw, unsigned fh,
                       int x, int y, uint32_t argb)
{
    if ((unsigned)x >= fw || (unsigned)y >= fh)
        return;
    if ((argb >> 24) < 8u)
        return;
    auto *p = reinterpret_cast<uint32_t *>(fb + (unsigned)y * pitch + (unsigned)x * 4u);
    *p = argb | 0xFF000000u;
}

static void splash_blit(unsigned char *fb, unsigned pitch, unsigned fw, unsigned fh,
                        int dst_x, int dst_y, const uint32_t *src, unsigned sw, unsigned sh)
{
    for (unsigned y = 0; y < sh; ++y) {
        for (unsigned x = 0; x < sw; ++x) {
            splash_put(fb, pitch, fw, fh, dst_x + (int)x, dst_y + (int)y, src[y * sw + x]);
        }
    }
}

static void splash_fill_white(unsigned char *fb, unsigned pitch, unsigned fw, unsigned fh)
{
    for (unsigned y = 0; y < fh; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(fb + (size_t)y * pitch);
        for (unsigned x = 0; x < fw; ++x)
            row[x] = 0xFFFFFFFFu;
    }
}

extern "C" int guest_splash_ready(void)
{
    return g_splash_fb_ready;
}

extern "C" int guest_splash_arm(void)
{
    if (g_splash_fb_ready)
        return 1;

    splash_fbinfo_wire_t fbinfo = {};
    long ret = bfree_guest_syscall1(BFREE_SYS_GET_FRAMEBUFFER_INFO, (long)&fbinfo);
    if (ret != 0 || !fbinfo.ready)
        ret = bfree_guest_syscall1(BFREE_SYS_GET_FRAMEBUFFER_INFO_LEGACY, (long)&fbinfo);
    if (ret != 0 || !fbinfo.ready || fbinfo.pitch == 0U || fbinfo.width == 0U
        || fbinfo.height == 0U || fbinfo.bpp != 32U)
        return 0;

    const uint64_t need_bytes = (uint64_t)fbinfo.pitch * (uint64_t)fbinfo.height;
    if (need_bytes == 0ULL || need_bytes > 0x7fffffffULL)
        return 0;

    const long mapped =
        bfree_guest_syscall5(BFREE_SYS_MMAP, 0L, (long)need_bytes, 0L, 0L, (long)BFREE_FB0_FD);
    if (mapped < 0L || (uint64_t)mapped != (uint64_t)BFREE_FB0_USER_MMAP_BASE)
        return 0;

    g_splash_fb_pitch = fbinfo.pitch;
    g_splash_fb_w = fbinfo.width;
    g_splash_fb_h = fbinfo.height;
    g_splash_fb_ready = 1;

    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    splash_fill_white(fb, g_splash_fb_pitch, g_splash_fb_w, g_splash_fb_h);
    guest_splash_show(0);
    return 1;
}

extern "C" void guest_splash_show(unsigned frame)
{
    if (!g_splash_fb_ready)
        return;
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = g_splash_fb_pitch;
    const unsigned fw = g_splash_fb_w;
    const unsigned fh = g_splash_fb_h;
    const int logo_x = ((int)fw - (int)g_splash_logo_w) / 2;
    const int logo_y = (int)fh / 2 + 24;
    const int plate_y0 = logo_y - 16;
    const int plate_y1 = logo_y + (int)g_splash_logo_h + 20 + 64;
    const int plate_x0 = ((int)fw - 360) / 2;
    const int plate_x1 = plate_x0 + 360;
    for (int y = plate_y0; y < plate_y1 && y < (int)fh; ++y) {
        if (y < 0)
            continue;
        for (int x = plate_x0; x < plate_x1; ++x)
            splash_put(fb, pitch, fw, fh, x, y, 0xFFFFFFFFu);
    }
    splash_blit(fb, pitch, fw, fh, logo_x, logo_y, g_splash_logo_px, g_splash_logo_w, g_splash_logo_h);
    if (g_splash_nframes == 0)
        return;
    unsigned fi = frame % g_splash_nframes;
    const unsigned sw = g_splash_sp_w[fi];
    const unsigned sh = g_splash_sp_h[fi];
    const int sp_x = ((int)fw - (int)sw) / 2;
    const int sp_y = logo_y + (int)g_splash_logo_h + 20;
    splash_blit(fb, pitch, fw, fh, sp_x, sp_y, g_splash_sp_px[fi], sw, sh);
    g_splash_frame = fi;
}

extern "C" void guest_splash_advance(void)
{
    if (!g_splash_fb_ready)
        return;
    g_splash_frame = (g_splash_frame + 1u) % (g_splash_nframes ? g_splash_nframes : 1u);
    guest_splash_show(g_splash_frame);
}
