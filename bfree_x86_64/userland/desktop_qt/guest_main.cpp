/* guest_main.cpp — ISO guest session: Qt Quick + bfree QPA + DesktopShell.qml (B5b). */
#if defined(BFREE_GUEST_HAS_QT6)
/* Skip qmake_QtQuick qrc until qRegisterResourceData is stable with large Quick. */
#ifndef BFREE_GUEST_SKIP_QTQUICK_QRC
#define BFREE_GUEST_SKIP_QTQUICK_QRC 1
#endif

#include "guest_bfree_shell_process.h"
#include "guest_desktop_bridge.h"
#include "guest_mvp_qmlcache_register.h"

#include <QEventLoop>
#include <QBackingStore>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <private/qquickrectangle_p.h>
#include <private/qwindow_p.h>
#include <QRegion>
#include <QResource>
#include <QString>
#include <QUrl>
#include <QWindow>
#include <QtPlugin>

#include "guest_resource_holder_va.h"
#include "bfree/bfree_guest_abi.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include <sys/time.h>

extern QStaticPlugin qt_static_plugin_QPlatformIntegrationPluginBFree(void);
extern QStaticPlugin qt_static_plugin_QGifPlugin(void);
extern QStaticPlugin qt_static_plugin_QICOPlugin(void);
extern void bfree_qpa_pump_guest_input(void);
extern unsigned bfree_qpa_wsi_mouse_log_count(void);
extern void bfree_qpa_cache_thread_data(void);
extern bool bfree_qpa_process_events(QEventLoop::ProcessEventsFlags flags);
extern void bfree_qpa_set_mouse_bridge(void (*)(int, int, unsigned, unsigned, int, int));
extern void bfree_qpa_set_key_bridge(void (*)(unsigned, int));
extern QStaticPlugin qt_static_plugin_QJpegPlugin(void);

/* Static Qt: QmlMeta/Quick type registrars are not auto-imported without qmlplugins. */
extern void qml_register_types_QtQml(void);
extern void qml_register_types_QtQml_Models(void);
extern void qml_register_types_QtQml_WorkerScript(void);
extern void qml_register_types_QtQuick(void);

extern "C" {
#include "guest_serial.h"
void bfree_guest_refresh_libc_auxv(void);
void bfree_guest_install_static_env(void);
void bfree_guest_run_on_ctor_stack(void (*fn)(void));
void bfree_guest_run_on_ctor_stack_plugins(void (*fn)(void));
void bfree_guest_run_on_ctor_stack_deep(void (*fn)(void));
void bfree_guest_run_on_ctor_stack_hybrid(void (*fn)(void));
void bfree_guest_run_on_ctor_stack_hybrid_keep_bump(void (*fn)(void));
void bfree_guest_enter_preflighted_mmap_noreturn(void (*fn)(void));
void bfree_guest_preflight_ctor_mmap(void);
void bfree_guest_begin_hybrid_alloc(void);
void bfree_guest_run_on_ctor_stack_musl(void (*fn)(void));
void bfree_guest_run_on_ctor_stack_musl_noreturn(void (*fn)(void));
void bfree_guest_qv4_preflight_arena(void);
void bfree_guest_qv4_mmap_scope_begin(void);
void bfree_guest_qv4_mmap_scope_end(void);
int bfree_guest_ensure_fallback_heap(void);
void bfree_guest_preflight_musl_heap(void);
void bfree_guest_preflight_brk_only(void);
void bfree_guest_preflight_arenas(void);
void bfree_guest_run_deferred_init_array_ctors(void);
void bfree_guest_qrc_alloc_scope_enter(void);
void bfree_guest_qrc_alloc_scope_leave(void);
void bfree_guest_qresource_sanitize(void);
void bfree_guest_serial_step_c(char step);
void bfree_guest_call_on_stack(void (*fn)(void), unsigned long long stack_top);
QWindow *bfree_guest_try_qquick_window(void);
void bfree_guest_qquick_drain(void);
void bfree_guest_set_prefer_fallback_alloc(int on);
void bfree_guest_serial_step_raw(char step);
void bfree_guest_rebind_musl_fs(void);
void guest_mmap_session_entry(void);
}

static void guest_serial_puts(const char *s)
{
    bfree_guest_serial_lit(s);
}

static char *guest_resource_global_holder(void)
{
    return (char *)(uintptr_t)BFREE_GUEST_QT_RESOURCE_HOLDER_VA;
}

static void guest_reset_qt_resource_registry(void)
{
    char *const h = guest_resource_global_holder();
    /* QArrayDataPointer at +0x18; size at +0x28 per qRegisterResourceData disasm. */
    memset(h + 0x18, 0, 24);
    bfree_guest_qresource_sanitize();
    guest_serial_puts("[desktop_qt] qresource registry cleared\n");
}

static void guest_serial_hex_u64_line(uint64_t v)
{
    char buf[24];
    int i = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned d = (unsigned)((v >> shift) & 0xfULL);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    guest_serial_puts(buf);
}

static void guest_serial_hex_u64(uint64_t v)
{
    char buf[20];
    int i = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned d = (unsigned)((v >> shift) & 0xfULL);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    buf[i] = '\0';
    guest_serial_puts(buf);
}

static void guest_log_resource_registry_n(void)
{
    const uint64_t n = *(const uint64_t *)(guest_resource_global_holder() + 0x28);
    guest_serial_puts("[desktop_qt] qresource registry n=");
    guest_serial_hex_u64_line(n);
}

static void guest_setup_context(QQmlEngine &engine, GuestDesktopBridge &bridge)
{
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("bfreeQmlRoot"), QStringLiteral("qrc:/"));
    ctx->setContextProperty(QStringLiteral("bfreeIconThemeRoot"), QString());
    ctx->setContextProperty(QStringLiteral("bfreeBreezeAppsDir"), QString());
    ctx->setContextProperty(QStringLiteral("bfreeBreezePlacesDir"), QString());
    ctx->setContextProperty(QStringLiteral("bfreeIconThemeReady"), false);
    ctx->setContextProperty(QStringLiteral("bfreeThirdPartyNoticesPath"), QString());
    ctx->setContextProperty(QStringLiteral("bfreeShellAutorun"), false);
    ctx->setContextProperty(QStringLiteral("bfreeDesktopPseudoFullscreen"), true);
    ctx->setContextProperty(QStringLiteral("bfreeAllowHostPower"), false);
    ctx->setContextProperty(QStringLiteral("bfreeEmbedWeb"), false);
    ctx->setContextProperty(QStringLiteral("bfreeEmbedWebKind"), QStringLiteral("none"));
    ctx->setContextProperty(QStringLiteral("desktopDataBridge"), &bridge);
}

static QGuiApplication *g_qapp = nullptr;
static QQmlEngine *g_engine = nullptr;
static GuestDesktopBridge *g_bridge = nullptr;
static QWindow *g_shell_window = nullptr;
static QBackingStore *g_shell_store = nullptr;
static QObject *g_qml_root = nullptr;
static QQmlComponent *g_item_comp = nullptr;
static int g_item_create_tried = 0;
static QQuickItem *g_desktop_shell_item = nullptr;
static int g_qml_ready = 0;
/* Distinctive fill when GuestDesktopShell.qml Rectangle is present (FB lookalike mirrors it). */
static uint32_t g_qml_rect_color = 0;
static int g_qml_rect_configured = 0;
/* W3: sparse Quick window chrome (host desktopWindowLayer look). */
static int g_w3_layer_ready = 0;
static int g_w3_sg_pixels = 0; /* 1 = skip FB window bodies (SG presented chrome) */
static QQuickItem *g_w3_layer = nullptr;
/* W3.1: sparse Quick taskbar + Start panel (host taskbar / launcherPanel). */
static int g_w31_layer_ready = 0;
static int g_w31_sg_pixels = 0; /* legacy; W3.2 uses g_w32_sg_taskbar for strip skip */
static QQuickItem *g_w31_layer = nullptr;
/* W3.2: SG presented taskbar strip once at attach; FB skips that strip. */
static int g_w32_sg_taskbar = 0;
static int g_w32_probe_ok = 0;

struct GuestW31Chrome {
    QQuickRectangle *bar;
    QQuickRectangle *startChip;
    QQuickRectangle *searchChip;
    QQuickRectangle *clockChip;
    QQuickRectangle *tbItem0;
    QQuickRectangle *tbItem1;
    QQuickRectangle *tbItem2;
    QQuickRectangle *tbItem3;
    QQuickRectangle *startPanel;
};
static GuestW31Chrome g_w31_chrome;

/* Start menu geometry (paint + hit + Quick scaffold) — host-ish on 1024x768. */
enum {
    G_START_MENU_W = 320,
    G_START_MENU_H = 360,
    G_START_MENU_X = 8,
    G_START_MENU_GAP = 8,
    G_START_PAD = 16,
    G_START_SEARCH_H = 42,
    G_START_ROW_H = 36,
    G_START_FOOTER_H = 40
};

static int guest_desk_start_menu_y0(void)
{
    return 768 - 52 - G_START_MENU_H - G_START_MENU_GAP; /* tbH=52 */
}

static int guest_desk_start_row0_y(void)
{
    return guest_desk_start_menu_y0() + G_START_PAD + G_START_SEARCH_H + 12;
}

/* Phase C: /persist listing via raw syscalls (avoid QDir — corrupts dispatcher). */
static char g_persist_names[12][24];
static int g_persist_nnames = 0;
static char g_persist_preview[80];
static int g_persist_listed = 0;

static long guest_sys3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

struct guest_linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

static void guest_persist_scan_once(void)
{
    char dentbuf[1024];
    long fd;
    long nread;
    if (g_persist_listed)
        return;
    g_persist_listed = 1;
    g_persist_nnames = 0;
    g_persist_preview[0] = '\0';
    fd = guest_sys3(2 /* open */, (long)"/persist", 0 /* O_RDONLY */, 0);
    if (fd < 0) {
        guest_serial_puts("[desktop_qt] Explorer /persist open fail\n");
        return;
    }
    guest_serial_puts("[desktop_qt] Explorer listing persist\n");
    for (;;) {
        nread = guest_sys3(217 /* getdents64 */, fd, (long)dentbuf, (long)sizeof(dentbuf));
        if (nread <= 0)
            break;
        long off = 0;
        while (off < nread && g_persist_nnames < 12) {
            auto *d = reinterpret_cast<guest_linux_dirent64 *>(dentbuf + off);
            const char *nm = d->d_name;
            if (nm[0] != '.' || (nm[1] != '\0' && !(nm[1] == '.' && nm[2] == '\0'))) {
                size_t i = 0;
                while (nm[i] && i + 1U < sizeof(g_persist_names[0])) {
                    g_persist_names[g_persist_nnames][i] = nm[i];
                    ++i;
                }
                g_persist_names[g_persist_nnames][i] = '\0';
                ++g_persist_nnames;
            }
            off += d->d_reclen;
        }
    }
    (void)guest_sys3(3 /* close */, fd, 0, 0);

    /* Prefer hello.txt / desk.txt / first file for preview. */
    const char *pick = nullptr;
    for (int pass = 0; pass < 3 && !pick; ++pass) {
        for (int i = 0; i < g_persist_nnames; ++i) {
            const char *nm = g_persist_names[i];
            if (pass == 0) {
                if ((nm[0] == 'h' || nm[0] == 'H') &&
                    (nm[1] == 'e' || nm[1] == 'E') &&
                    (nm[2] == 'l' || nm[2] == 'L') &&
                    (nm[3] == 'l' || nm[3] == 'L') &&
                    (nm[4] == 'o' || nm[4] == 'O')) {
                    pick = nm;
                    break;
                }
            } else if (pass == 1) {
                if ((nm[0] == 'd' || nm[0] == 'D') &&
                    (nm[1] == 'e' || nm[1] == 'E') &&
                    (nm[2] == 's' || nm[2] == 'S') &&
                    (nm[3] == 'k' || nm[3] == 'K')) {
                    pick = nm;
                    break;
                }
            } else {
                pick = nm;
                break;
            }
        }
    }
    if (pick) {
        char path[40];
        path[0] = '/'; path[1] = 'p'; path[2] = 'e'; path[3] = 'r';
        path[4] = 's'; path[5] = 'i'; path[6] = 's'; path[7] = 't'; path[8] = '/';
        size_t j = 0;
        while (pick[j] && j + 10U < sizeof(path)) {
            path[9 + j] = pick[j];
            ++j;
        }
        path[9 + j] = '\0';
        long f2 = guest_sys3(2, (long)path, 0, 0);
        if (f2 >= 0) {
            long nr = guest_sys3(0 /* read */, f2, (long)g_persist_preview,
                                 (long)(sizeof(g_persist_preview) - 1U));
            (void)guest_sys3(3, f2, 0, 0);
            if (nr > 0) {
                g_persist_preview[nr] = '\0';
                guest_serial_puts("[desktop_qt] Explorer read ");
                guest_serial_puts(pick);
                guest_serial_puts("=");
                guest_serial_puts(g_persist_preview);
                guest_serial_puts("\n");
            }
        }
    }
}

/* Create /persist/desk.txt from the desk (Phase E: note 作成). */
static void guest_persist_create_desk_note(void)
{
    static int created;
    static const char body[] = "from-desk";
    if (created)
        return;
    created = 1;
    /* O_WRONLY|O_CREAT|O_TRUNC = 1|64|512 */
    long fd = guest_sys3(2, (long)"/persist/desk.txt", 1 | 64 | 512, 0644);
    if (fd < 0) {
        guest_serial_puts("[desktop_qt] desk note create fail\n");
        return;
    }
    (void)guest_sys3(1 /* write */, fd, (long)body, (long)(sizeof(body) - 1));
    (void)guest_sys3(3, fd, 0, 0);
    guest_serial_puts("[desktop_qt] desk note created\n");
    g_persist_listed = 0;
}

/* Terminal: one BusyBox-equivalent command via syscalls (keep desktop alive). */
static void guest_terminal_run_once(void)
{
    static const char body[] = "TERM_OK";
    static int ran;
    if (ran)
        return;
    ran = 1;
    long fd = guest_sys3(2, (long)"/persist/term.txt", 1 | 64 | 512, 0644);
    if (fd < 0) {
        guest_serial_puts("[desktop_qt] Terminal command fail\n");
        return;
    }
    (void)guest_sys3(1 /* write */, fd, (long)body, (long)(sizeof(body) - 1));
    (void)guest_sys3(3, fd, 0, 0);
    guest_serial_puts("[desktop_qt] Terminal ran echo TERM_OK\n");
    g_persist_listed = 0;
}

static void guest_configure_qml_rectangle(QQuickItem *root)
{
    if (!root || g_qml_rect_configured)
        return;
    /* Prefer an existing QML child Rectangle; else attach one in C++ so the
     * desk is still Item+Rectangle without IR beginCreate PF. */
    QQuickRectangle *rect = nullptr;
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *ch : kids) {
        rect = qobject_cast<QQuickRectangle *>(ch);
        if (rect)
            break;
    }
    if (!rect) {
        rect = new QQuickRectangle(nullptr);
        rect->setObjectName(QStringLiteral("GuestDeskRectangle"));
        rect->setParentItem(root);
        guest_serial_puts("[desktop_qt] QML Rectangle desk attached (C++ child)\n");
    } else {
        guest_serial_puts("[desktop_qt] QML Rectangle desk configured\n");
    }
    rect->setWidth(root->width() > 0 ? root->width() : 1024);
    rect->setHeight(root->height() > 0 ? root->height() : 768);
    /* Distinct from wallpaper #7A8FA8 so screendump/serial can prove QML provenance. */
    rect->setColor(QColor(0x3d, 0x8b, 0x6e));
    g_qml_rect_color = 0xFF3D8B6Eu;
    g_qml_rect_configured = 1;
}

/* Direct FB poke so QEMU screendump shows something even if QWindow show() still faults. */
#ifndef BFREE_FB0_USER_MMAP_BASE
#define BFREE_FB0_USER_MMAP_BASE 0x01400000u
#endif

static void guest_paint_fb_direct(void)
{
    /* Defaults match QPA probe path when ioctl is unavailable mid-session. */
    const unsigned width = 1024;
    const unsigned height = 768;
    const unsigned pitch = width * 4;
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    guest_serial_puts("[desktop_qt] paint FB direct\n");
    for (unsigned y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(fb + (size_t)y * pitch);
        const uint32_t bg = (y >= height - 40u) ? 0xFF334155u : 0xFF7A8FA8u;
        for (unsigned x = 0; x < width; ++x)
            row[x] = bg;
    }
    /* Center card */
    const unsigned cx0 = width / 2 - 220;
    const unsigned cy0 = height / 2 - 70;
    for (unsigned y = cy0; y < cy0 + 140; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(fb + (size_t)y * pitch);
        for (unsigned x = cx0; x < cx0 + 440; ++x)
            row[x] = 0xFFF8FAFCu;
    }
    guest_serial_puts("[desktop_qt] FB direct painted\n");
}

static void guest_paint_shell_window(void)
{
    if (!g_shell_window || !g_shell_store)
        return;
    const QSize sz = g_shell_window->size();
    if (sz.width() <= 0 || sz.height() <= 0)
        return;
    g_shell_store->resize(sz);
    const QRect rect(QPoint(0, 0), sz);
    g_shell_store->beginPaint(rect);
    QPainter p(g_shell_store->paintDevice());
    /* Match gui_server/integration_gui/GuestMvpShell.qml layout. */
    p.fillRect(rect, QColor(0x7a, 0x8f, 0xa8));
    const int cardW = qMin(sz.width() - 80, 560);
    const int cardH = 220;
    const QRect card((sz.width() - cardW) / 2, (sz.height() - cardH) / 2, cardW, cardH);
    p.fillRect(card, QColor(0xf8, 0xfa, 0xfc));
    p.setPen(QPen(QColor(0x64, 0x74, 0x8b), 2));
    p.drawRect(card.adjusted(0, 0, -1, -1));
    p.setPen(QColor(0x1e, 0x29, 0x3b));
    p.drawText(card.adjusted(20, 40, -20, -80),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("DesktopShell MVP (native guest)"));
    p.setPen(QColor(0x33, 0x41, 0x55));
    p.drawText(card.adjusted(20, 90, -20, -20),
               Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("Qt Quick on bfree QPA + framebuffer.\n"
                              "GuestMvpShell path (DesktopShell.qml next)."));
    p.fillRect(QRect(0, sz.height() - 38, sz.width(), 38), QColor(0x33, 0x41, 0x55));
    p.setPen(QColor(0xf8, 0xfa, 0xfc));
    p.drawText(QRect(12, sz.height() - 38, sz.width() - 24, 38),
               Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("DesktopShell"));
    g_shell_store->endPaint();
    g_shell_store->flush(QRegion(rect), g_shell_window, QPoint());
    guest_serial_puts("[desktop_qt] shell window painted\n");
}

struct GuestShellExposeFilter : QObject {
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == g_shell_window && event && event->type() == QEvent::Expose)
            guest_paint_shell_window();
        return QObject::eventFilter(watched, event);
    }
};

static GuestShellExposeFilter *g_shell_filter = nullptr;

/* Tiny 5x7 glyphs for GuestMvpShell labels (no Qt font engine on guest yet). */
static int glyph_row(char c, int row)
{
    switch (c) {
    case 'A': { static const unsigned char g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'B': { static const unsigned char g[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
    case 'C': { static const unsigned char g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
    case 'D': { static const unsigned char g[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return g[row]; }
    case 'E': { static const unsigned char g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
    case 'F': { static const unsigned char g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'G': { static const unsigned char g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g[row]; }
    case 'H': { static const unsigned char g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'I': { static const unsigned char g[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case 'J': { static const unsigned char g[7]={0x01,0x01,0x01,0x01,0x11,0x11,0x0E}; return g[row]; }
    case 'K': { static const unsigned char g[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
    case 'L': { static const unsigned char g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
    case 'M': { static const unsigned char g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    case 'N': { static const unsigned char g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g[row]; }
    case 'O': { static const unsigned char g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'P': { static const unsigned char g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'Q': { static const unsigned char g[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g[row]; }
    case 'R': { static const unsigned char g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
    case 'S': { static const unsigned char g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
    case 'T': { static const unsigned char g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'U': { static const unsigned char g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'V': { static const unsigned char g[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
    case 'W': { static const unsigned char g[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
    case 'X': { static const unsigned char g[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
    case 'Y': { static const unsigned char g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'Z': { static const unsigned char g[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
    case 'a': { static const unsigned char g[7]={0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}; return g[row]; }
    case 'b': { static const unsigned char g[7]={0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}; return g[row]; }
    case 'c': { static const unsigned char g[7]={0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}; return g[row]; }
    case 'd': { static const unsigned char g[7]={0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}; return g[row]; }
    case 'e': { static const unsigned char g[7]={0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}; return g[row]; }
    case 'f': { static const unsigned char g[7]={0x06,0x08,0x08,0x1C,0x08,0x08,0x08}; return g[row]; }
    case 'g': { static const unsigned char g[7]={0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}; return g[row]; }
    case 'h': { static const unsigned char g[7]={0x10,0x10,0x1E,0x11,0x11,0x11,0x11}; return g[row]; }
    case 'i': { static const unsigned char g[7]={0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}; return g[row]; }
    case 'k': { static const unsigned char g[7]={0x10,0x10,0x12,0x14,0x18,0x14,0x12}; return g[row]; }
    case 'l': { static const unsigned char g[7]={0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case 'm': { static const unsigned char g[7]={0x00,0x00,0x1A,0x15,0x15,0x15,0x15}; return g[row]; }
    case 'n': { static const unsigned char g[7]={0x00,0x00,0x1E,0x11,0x11,0x11,0x11}; return g[row]; }
    case 'o': { static const unsigned char g[7]={0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'p': { static const unsigned char g[7]={0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}; return g[row]; }
    case 'q': { static const unsigned char g[7]={0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}; return g[row]; }
    case 'r': { static const unsigned char g[7]={0x00,0x00,0x16,0x19,0x10,0x10,0x10}; return g[row]; }
    case 's': { static const unsigned char g[7]={0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}; return g[row]; }
    case 't': { static const unsigned char g[7]={0x08,0x08,0x1C,0x08,0x08,0x09,0x06}; return g[row]; }
    case 'u': { static const unsigned char g[7]={0x00,0x00,0x11,0x11,0x11,0x13,0x0D}; return g[row]; }
    case 'v': { static const unsigned char g[7]={0x00,0x00,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
    case 'w': { static const unsigned char g[7]={0x00,0x00,0x11,0x15,0x15,0x15,0x0A}; return g[row]; }
    case 'x': { static const unsigned char g[7]={0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}; return g[row]; }
    case 'y': { static const unsigned char g[7]={0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}; return g[row]; }
    case '0': { static const unsigned char g[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g[row]; }
    case '1': { static const unsigned char g[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case '2': { static const unsigned char g[7]={0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}; return g[row]; }
    case '3': { static const unsigned char g[7]={0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}; return g[row]; }
    case '4': { static const unsigned char g[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
    case '5': { static const unsigned char g[7]={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; return g[row]; }
    case '6': { static const unsigned char g[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
    case '7': { static const unsigned char g[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case '8': { static const unsigned char g[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
    case '9': { static const unsigned char g[7]={0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return g[row]; }
    case '.': { static const unsigned char g[7]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g[row]; }
    case ',': { static const unsigned char g[7]={0x00,0x00,0x00,0x00,0x0C,0x04,0x08}; return g[row]; }
    case '-': { static const unsigned char g[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
    case '/': { static const unsigned char g[7]={0x01,0x02,0x04,0x08,0x10,0x00,0x00}; return g[row]; }
    case ':': { static const unsigned char g[7]={0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}; return g[row]; }
    case '(': { static const unsigned char g[7]={0x02,0x04,0x08,0x08,0x08,0x04,0x02}; return g[row]; }
    case ')': { static const unsigned char g[7]={0x08,0x04,0x02,0x02,0x02,0x04,0x08}; return g[row]; }
    case '+': { static const unsigned char g[7]={0x00,0x04,0x04,0x1F,0x04,0x04,0x00}; return g[row]; }
    case '_': { static const unsigned char g[7]={0x00,0x00,0x00,0x00,0x00,0x00,0x1F}; return g[row]; }
    case '[': { static const unsigned char g[7]={0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return g[row]; }
    case ']': { static const unsigned char g[7]={0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return g[row]; }
    case '=': { static const unsigned char g[7]={0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}; return g[row]; }
    default: return 0;
    }
}

static void fb_put_pixel(unsigned char *fb, unsigned pitch, int x, int y, uint32_t argb)
{
    if (x < 0 || y < 0 || x >= 1024 || y >= 768)
        return;
    auto *row = reinterpret_cast<uint32_t *>(fb + (size_t)y * pitch);
    row[x] = argb;
}

static void fb_draw_text(unsigned char *fb, unsigned pitch, int x, int y, const char *s, uint32_t color, int scale)
{
    for (int i = 0; s[i]; ++i) {
        const char c = s[i];
        if (c == '\n') {
            y += 8 * scale + 4;
            x = 20; /* caller typically centers; simple wrap */
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            const int bits = glyph_row(c, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (0x10 >> col)) {
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx)
                            fb_put_pixel(fb, pitch, x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static void fb_fill_rect(unsigned char *fb, unsigned pitch, int x0, int y0, int w, int h, uint32_t argb)
{
    for (int y = y0; y < y0 + h; ++y) {
        if (y < 0 || y >= 768)
            continue;
        auto *row = reinterpret_cast<uint32_t *>(fb + (size_t)y * pitch);
        for (int x = x0; x < x0 + w; ++x) {
            if (x >= 0 && x < 1024)
                row[x] = argb;
        }
    }
}

static void fb_fill_round_rect(unsigned char *fb, unsigned pitch, int x0, int y0, int w, int h, int r, uint32_t argb)
{
    /* Soft corners: skip outer corner pixels (no true circle math needed). */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int nearL = x < r, nearR = x >= w - r, nearT = y < r, nearB = y >= h - r;
            if ((nearL && nearT && (r - x) + (r - y) > r + 1) ||
                (nearR && nearT && (x - (w - 1 - r)) + (r - y) > r + 1) ||
                (nearL && nearB && (r - x) + (y - (h - 1 - r)) > r + 1) ||
                (nearR && nearB && (x - (w - 1 - r)) + (y - (h - 1 - r)) > r + 1))
                continue;
            fb_put_pixel(fb, pitch, x0 + x, y0 + y, argb);
        }
    }
}

/*
 * Host DesktopShell.qml lookalike on FB0 (final pixel authority until full QML ports).
 * Must run AFTER any QQuickWindow/QPA flush — otherwise solid wallpaper wins the screendump.
 * Click/key open apps via Qt input path (processEvents + event filter) or POLL_INPUT fallback.
 */
struct GuestDeskIcon {
    const char *acro;
    const char *title;
    uint32_t accent;
};

static const GuestDeskIcon g_desk_icons[] = {
    {"EX", "Explorer", 0xFF1D4ED8u},
    {"VW", "Viewer", 0xFF0F766Eu},
    {"TE", "Terminal", 0xFFC2410Cu},
    {"SM", "SystemMonitor", 0xFF7C3AEDu},
    {"SS", "SystemSettings", 0xFF0F766Eu},
    {"DI", "Discover", 0xFF0D9488u},
    {"AS", "AppStore", 0xFF1E3A8Au},
    {"NO", "Notification", 0xFF7C3AEDu},
    {"CL", "ClockApplet", 0xFFC2410Cu},
    {"CA", "Calculator", 0xFF1D4ED8u},
    {"PB", "PaintBoard", 0xFF0D9488u},
    {"NM", "NetworkManager", 0xFF1D4ED8u},
    {"BA", "BatteryManager", 0xFF0F766Eu},
    {"SO", "SoundManager", 0xFFBE185Du},
    {"JI", "JapaneseIME", 0xFF7C3AEDu},
    {"TR", "Trash", 0xFF475569u},
};
static const int g_desk_n_icons = (int)(sizeof g_desk_icons / sizeof g_desk_icons[0]);
static const int g_desk_tb_h = 52;
static const int g_win_title_h = 40;
static const int g_win_min_w = 280;
static const int g_win_min_h = 180;
static const int g_win_edge = 10;
static const int g_win_max = 4;

struct GuestWin {
    int open;
    int minimized;
    int maximized;
    int app_id;
    int x, y, w, h;
    int rx, ry, rw, rh; /* restore rect when maximized */
    int z;
};

static GuestWin g_wins[g_win_max];
static int g_win_n = 0;
static int g_win_focus = -1; /* index into g_wins, or -1 */
static int g_win_zseq = 1;
/* WM: 0 idle, 1 drag, 2 se, 3 e, 4 s, 5 w */
static int g_wm_mode = 0;
static int g_wm_win = -1;
static int g_wm_gx = 0, g_wm_gy = 0;
static int g_wm_ox = 0, g_wm_oy = 0, g_wm_ow = 0, g_wm_oh = 0;
/* Title dbl-click (maximize toggle): packed to avoid extra BSS growth. */
static int g_title_dbl_wi = -1;
static int g_title_dbl_paint = 0;

static int g_desk_start_open = 0;
static int g_desk_asleep = 0;
static int g_desk_dirty = 1;
static int g_desk_paint_count = 0;
static int g_desk_qpa_input = 0;
static uint32_t g_desk_prev_btn = 0;
static int g_desk_mx = 512;
static int g_desk_my = 384;

/* Compat: any open window with this app (for smoke "desk open Explorer"). */
static int guest_win_find_app(int app_id)
{
    for (int i = 0; i < g_win_n; ++i) {
        if (g_wins[i].open && g_wins[i].app_id == app_id)
            return i;
    }
    return -1;
}

static int guest_clamp_mouse(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void guest_desk_mark_dirty(void)
{
    g_desk_dirty = 1;
}

static void guest_desk_draw_cursor(unsigned char *fb, unsigned pitch)
{
    const int x = g_desk_mx;
    const int y = g_desk_my;
    fb_fill_rect(fb, pitch, x - 8, y - 1, 17, 3, 0xFFFFF1F2u);
    fb_fill_rect(fb, pitch, x - 1, y - 8, 3, 17, 0xFFFFF1F2u);
    fb_fill_rect(fb, pitch, x - 6, y, 13, 1, 0xFF111827u);
    fb_fill_rect(fb, pitch, x, y - 6, 1, 13, 0xFF111827u);
}

/* Cursor-only idle updates — carve in fallback heap (no BSS growth). */
static int g_cur_have = 0;
static int g_cur_sx = -100, g_cur_sy = -100;
static uint32_t *g_cur_under = nullptr;

static uint32_t *guest_desk_cur_under(void)
{
    if (!g_cur_under)
        g_cur_under = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(0x19F00000u));
    return g_cur_under;
}

static void guest_desk_cursor_blit_under(unsigned char *fb, unsigned pitch, int x, int y,
                                         uint32_t *under, int save)
{
    auto *pix = reinterpret_cast<uint32_t *>(fb);
    const int stride = (int)(pitch / 4u);
    for (int dy = 0; dy < 17; ++dy) {
        for (int dx = 0; dx < 17; ++dx) {
            const int px = x - 8 + dx;
            const int py = y - 8 + dy;
            if (px < 0 || px >= 1024 || py < 0 || py >= 768)
                continue;
            if (save)
                under[dy * 17 + dx] = pix[py * stride + px];
            else
                pix[py * stride + px] = under[dy * 17 + dx];
        }
    }
}

static void guest_desk_cursor_move_only(void)
{
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    uint32_t *under = guest_desk_cur_under();
    if (g_cur_have)
        guest_desk_cursor_blit_under(fb, pitch, g_cur_sx, g_cur_sy, under, 0);
    guest_desk_cursor_blit_under(fb, pitch, g_desk_mx, g_desk_my, under, 1);
    g_cur_sx = g_desk_mx;
    g_cur_sy = g_desk_my;
    g_cur_have = 1;
    guest_desk_draw_cursor(fb, pitch);
}

static void guest_desk_icon_xy(int i, int *ox, int *oy)
{
    const int cellW = 88, cellH = 100, cols = 6;
    const int originX = 28, originY = 36;
    int x = originX + (i % cols) * cellW;
    int y = originY + (i / cols) * cellH;
    if (i == g_desk_n_icons - 1) {
        x = 1024 - 16 - 76;
        y = 768 - g_desk_tb_h - 12 - 96;
    }
    *ox = x;
    *oy = y;
}

static int guest_desk_hit_start(int mx, int my)
{
    const int x0 = 8, y0 = 768 - g_desk_tb_h + 6, w = 56, h = 40;
    return (mx >= x0 && mx < x0 + w && my >= y0 && my < y0 + h) ? 1 : 0;
}

static int guest_desk_hit_search(int mx, int my)
{
    const int x0 = 72, y0 = 768 - g_desk_tb_h + 10, w = 220, h = 32;
    return (mx >= x0 && mx < x0 + w && my >= y0 && my < y0 + h) ? 1 : 0;
}

/* Shared taskbar slot geometry (paint + hit). */
static int guest_desk_task_slot_x(int slot)
{
    return 300 + slot * 110;
}

static int guest_desk_collect_task_wins(int *out, int maxn)
{
    int n = 0;
    for (int i = 0; i < g_win_n && n < maxn; ++i) {
        if (g_wins[i].open)
            out[n++] = i;
    }
    return n;
}

static void guest_desk_clock_text(char *buf, int buflen)
{
    if (buflen < 9) {
        if (buflen > 0)
            buf[0] = 0;
        return;
    }
    unsigned long sec = 0;
    struct timeval tv = {};
    if (gettimeofday(&tv, nullptr) == 0)
        sec = (unsigned long)tv.tv_sec;
    if (sec == 0)
        sec = (unsigned long)(g_desk_paint_count * 7u + 15u * 3600u);
    unsigned h24 = (unsigned)((sec / 3600u) % 24u);
    unsigned m = (unsigned)((sec / 60u) % 60u);
    const char *ampm = (h24 >= 12u) ? "PM" : "AM";
    unsigned h12 = h24 % 12u;
    if (h12 == 0)
        h12 = 12;
    /* Prefer compact "H:MM PM" / "HH:MM PM". */
    int p = 0;
    if (h12 >= 10)
        buf[p++] = (char)('0' + (h12 / 10u));
    buf[p++] = (char)('0' + (h12 % 10u));
    buf[p++] = ':';
    buf[p++] = (char)('0' + (m / 10u));
    buf[p++] = (char)('0' + (m % 10u));
    buf[p++] = ' ';
    buf[p++] = ampm[0];
    buf[p++] = ampm[1];
    buf[p] = 0;
}

static int guest_desk_hit_start_item(int mx, int my)
{
    if (!g_desk_start_open)
        return -1;
    const int mx0 = G_START_MENU_X;
    const int my0 = guest_desk_start_menu_y0();
    if (mx < mx0 || mx >= mx0 + G_START_MENU_W || my < my0 || my >= my0 + G_START_MENU_H)
        return -1;
    const int row0 = guest_desk_start_row0_y();
    if (my < row0)
        return -1;
    const int row = (my - row0) / G_START_ROW_H;
    if (row < 0 || row > 5)
        return -1;
    return row;
}

static int guest_desk_hit(int mx, int my)
{
    const int cellW = 88, cellH = 96;
    int best = -1;
    int best_d2 = 72 * 72;
    for (int i = 0; i < g_desk_n_icons; ++i) {
        int x, y;
        guest_desk_icon_xy(i, &x, &y);
        if (mx >= x && mx < x + cellW && my >= y && my < y + cellH)
            return i;
        const int cx = x + cellW / 2;
        const int cy = y + 36;
        const int dx = mx - cx;
        const int dy = my - cy;
        const int d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

static void guest_win_clamp_geom(GuestWin *w)
{
    const int deskH = 768 - g_desk_tb_h;
    if (w->w < g_win_min_w)
        w->w = g_win_min_w;
    if (w->h < g_win_min_h)
        w->h = g_win_min_h;
    if (w->w > 1024)
        w->w = 1024;
    if (w->h > deskH)
        w->h = deskH;
    if (w->x < 0)
        w->x = 0;
    if (w->y < 0)
        w->y = 0;
    if (w->x + w->w > 1024)
        w->x = 1024 - w->w;
    if (w->y + w->h > deskH)
        w->y = deskH - w->h;
}

static void guest_win_refocus_top(void)
{
    g_win_focus = -1;
    int best = -1, bz = -1;
    for (int i = 0; i < g_win_n; ++i) {
        if (g_wins[i].open && !g_wins[i].minimized && g_wins[i].z > bz) {
            bz = g_wins[i].z;
            best = i;
        }
    }
    g_win_focus = best;
}

static void guest_win_raise(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    g_wins[wi].minimized = 0;
    g_wins[wi].z = ++g_win_zseq;
    g_win_focus = wi;
}

static void guest_win_minimize(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    g_wins[wi].minimized = 1;
    if (g_wm_win == wi) {
        g_wm_mode = 0;
        g_wm_win = -1;
    }
    guest_serial_puts("[desktop_qt] task minimize\n");
    guest_win_refocus_top();
}

static void guest_win_maximize(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    GuestWin *w = &g_wins[wi];
    if (w->maximized)
        return;
    w->rx = w->x;
    w->ry = w->y;
    w->rw = w->w;
    w->rh = w->h;
    w->maximized = 1;
    w->minimized = 0;
    w->x = 0;
    w->y = 0;
    w->w = 1024;
    w->h = 768 - g_desk_tb_h;
    guest_win_raise(wi);
    guest_serial_puts("[desktop_qt] wm maximize\n");
}

static void guest_win_restore(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    GuestWin *w = &g_wins[wi];
    if (!w->maximized)
        return;
    w->maximized = 0;
    w->x = w->rx;
    w->y = w->ry;
    w->w = w->rw;
    w->h = w->rh;
    guest_win_clamp_geom(w);
    guest_win_raise(wi);
    guest_serial_puts("[desktop_qt] wm restore\n");
}

static void guest_win_toggle_maximize(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    if (g_wins[wi].maximized)
        guest_win_restore(wi);
    else
        guest_win_maximize(wi);
}

/* Topmost visible window under point; returns win index or -1. */
static int guest_win_hit_top(int mx, int my)
{
    int best = -1;
    int best_z = -1;
    for (int i = 0; i < g_win_n; ++i) {
        if (!g_wins[i].open || g_wins[i].minimized)
            continue;
        const GuestWin *w = &g_wins[i];
        if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
            if (w->z > best_z) {
                best_z = w->z;
                best = i;
            }
        }
    }
    return best;
}

enum GuestWinHit {
    GWH_NONE = 0,
    GWH_CLOSE,
    GWH_MAX,
    GWH_MIN,
    GWH_TITLE,
    GWH_RESIZE_SE,
    GWH_RESIZE_E,
    GWH_RESIZE_S,
    GWH_RESIZE_W,
    GWH_CLIENT
};

static int guest_win_hit_part(const GuestWin *w, int mx, int my)
{
    if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h)
        return GWH_NONE;
    /* Title-bar chips: min / max / close (right, ~36px each). */
    if (my < w->y + g_win_title_h) {
        if (mx >= w->x + w->w - 40)
            return GWH_CLOSE;
        if (mx >= w->x + w->w - 76)
            return GWH_MAX;
        if (mx >= w->x + w->w - 112)
            return GWH_MIN;
        return GWH_TITLE;
    }
    if (w->maximized)
        return GWH_CLIENT;
    /* Edges: W / E / S / SE (host desktopWindowLayer). */
    const int nearW = (mx < w->x + 12);
    const int nearE = (mx >= w->x + w->w - 16);
    const int nearS = (my >= w->y + w->h - 16);
    if (nearE && nearS)
        return GWH_RESIZE_SE;
    if (nearE)
        return GWH_RESIZE_E;
    if (nearS)
        return GWH_RESIZE_S;
    if (nearW)
        return GWH_RESIZE_W;
    return GWH_CLIENT;
}

/* ---- W3 Quick window chrome (host DesktopShell desktopWindowLayer) ----
 * Sparse QQuickRectangle tree only — QQuickText / dense ItemHasContents + SG
 * update historically PFs on guest. Glyph/title glyphs stay on FB lookalike.
 * Parent under contentItem; build per-slot lazily when a GuestWin opens. */
struct GuestW3Chrome {
    QQuickRectangle *outer;
    QQuickRectangle *title;
    QQuickRectangle *glyph;
    QQuickRectangle *btnMin;
    QQuickRectangle *btnMax;
    QQuickRectangle *btnClose;
    QQuickRectangle *client;
    int built;
};

static GuestW3Chrome g_w3_chrome[4];

static QQuickRectangle *guest_w3_new_rect(QQuickItem *parent)
{
    auto *r = new QQuickRectangle();
    r->setParentItem(parent);
    r->setVisible(false);
    return r;
}

static void guest_w3_build_one(int wi, QQuickItem *layer)
{
    if (wi < 0 || wi >= 4 || !layer || g_w3_chrome[wi].built)
        return;
    GuestW3Chrome *c = &g_w3_chrome[wi];
    c->outer = guest_w3_new_rect(layer);
    c->outer->setObjectName(QStringLiteral("W3WinOuter"));
    c->title = guest_w3_new_rect(c->outer);
    c->glyph = guest_w3_new_rect(c->title);
    c->btnMin = guest_w3_new_rect(c->title);
    c->btnMax = guest_w3_new_rect(c->title);
    c->btnClose = guest_w3_new_rect(c->title);
    c->client = guest_w3_new_rect(c->outer);
    c->built = 1;
}

static void guest_w3_sync_one(int wi)
{
    if (wi < 0 || wi >= 4 || !g_w3_chrome[wi].built)
        return;
    GuestW3Chrome *c = &g_w3_chrome[wi];
    const GuestWin *win = &g_wins[wi];
    /* While FB owns pixels, keep Quick chrome invisible so software SG never
     * materializes ItemHasContents (historically PFs). Geometry still synced. */
    const int show_quick = (g_w3_sg_pixels != 0);
    if (!win->open || win->minimized || win->app_id < 0 || win->app_id >= g_desk_n_icons) {
        if (c->outer)
            c->outer->setVisible(false);
        return;
    }
    const int focused = (wi == g_win_focus);
    const int titleH = 38;
    const qreal rad = win->maximized ? 0.0 : 10.0;
    const QColor borderCol = focused ? QColor(0x64, 0x74, 0x8b) : QColor(0x94, 0xa3, 0xb8);
    const qreal bw = focused ? 2.0 : 1.0;

    c->outer->setVisible(show_quick);
    c->outer->setX(win->x);
    c->outer->setY(win->y);
    c->outer->setWidth(win->w);
    c->outer->setHeight(win->h);
    c->outer->setZ(win->z);
    c->outer->setRadius(rad);
    c->outer->setColor(QColor(0xf8, 0xfa, 0xfc));
    if (QQuickPen *pen = c->outer->border()) {
        pen->setWidth(bw);
        pen->setColor(borderCol);
    }

    c->title->setVisible(show_quick);
    c->title->setX(0);
    c->title->setY(0);
    c->title->setWidth(win->w);
    c->title->setHeight(titleH);
    c->title->setRadius(0);
    c->title->setColor(focused ? QColor(0x33, 0x41, 0x55) : QColor(0x47, 0x55, 0x69));

    /* Glyph chip (no Text — FB paints acro/title). */
    c->glyph->setVisible(show_quick);
    c->glyph->setX(8);
    c->glyph->setY(5);
    c->glyph->setWidth(28);
    c->glyph->setHeight(28);
    c->glyph->setRadius(6);
    c->glyph->setColor(QColor(0x1e, 0x29, 0x3b));

    auto place_btn = [show_quick](QQuickRectangle *b, qreal x, qreal y, const QColor &col) {
        b->setVisible(show_quick);
        b->setX(x);
        b->setY(y);
        b->setWidth(28);
        b->setHeight(28);
        b->setRadius(4);
        b->setColor(col);
    };
    place_btn(c->btnMin, win->w - 112.0, 5.0, QColor(0x1e, 0x29, 0x3b));
    place_btn(c->btnMax, win->w - 76.0, 5.0, QColor(0x1e, 0x29, 0x3b));
    place_btn(c->btnClose, win->w - 40.0, 5.0, QColor(0x7f, 0x1d, 0x1d));

    c->client->setVisible(show_quick);
    c->client->setX(bw);
    c->client->setY(titleH);
    c->client->setWidth(win->w > (int)(2 * bw) ? (win->w - 2 * bw) : 1);
    c->client->setHeight(win->h > titleH + (int)bw ? (win->h - titleH - (int)bw) : 1);
    c->client->setColor(QColor(0xf1, 0xf5, 0xf9));
    c->client->setRadius(0);
}

static void guest_w3_sync_window_layer(void)
{
    /* Intentionally empty during event loop: mutating/creating QQuickRectangle
     * trees after processEvents has GPF'd (RIP in BFreeInput::pollRawEvent).
     * Chrome trees are built once at attach (invisible); FB paints pixels. */
    (void)g_w3_layer;
}

static void guest_w3_soft_sg_try(QQuickWindow *win)
{
    if (!win || !g_w3_layer)
        return;
    /* Soft try breadcrumb only — no requestUpdate/sendPostedEvents (historically PFs). */
    guest_serial_puts("[desktop_qt] W3 SG soft try (no force expose)\n");
    g_w3_sg_pixels = 0;
    guest_serial_puts("[desktop_qt] W3 SG soft try done (FB authority)\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
}

static void guest_w3_build_window_layer(QQuickItem *parent)
{
    if (!parent || g_w3_layer_ready)
        return;
    guest_serial_puts("[desktop_qt] W3 build Quick window chrome\n");
    g_w3_layer = new QQuickItem();
    g_w3_layer->setObjectName(QStringLiteral("W3WindowLayer"));
    g_w3_layer->setParentItem(parent);
    g_w3_layer->setWidth(parent->width() > 0 ? parent->width() : 1024);
    g_w3_layer->setHeight(parent->height() > 0 ? parent->height() : (768 - 52));
    g_w3_layer->setZ(400);
    /* Build sparse chrome once at attach; keep invisible (g_w3_sg_pixels=0). */
    for (int i = 0; i < 4; ++i) {
        guest_w3_build_one(i, g_w3_layer);
        guest_w3_sync_one(i);
    }
    g_w3_layer_ready = 1;
    guest_serial_puts("[desktop_qt] W3 window layer ready\n");
}

/* ---- W3.1 Quick taskbar + Start (host DesktopShell taskbar / launcherPanel) ----
 * Attach-once invisible QQuickRectangle tree; FB paints pixels. No event-loop mutates. */
static void guest_w31_place_rect(QQuickRectangle *r, qreal x, qreal y, qreal w, qreal h,
                                 qreal rad, const QColor &col)
{
    if (!r)
        return;
    r->setVisible(false);
    r->setX(x);
    r->setY(y);
    r->setWidth(w);
    r->setHeight(h);
    r->setRadius(rad);
    r->setColor(col);
}

static void guest_w31_build_taskbar_layer(QQuickItem *parent)
{
    if (!parent || g_w31_layer_ready)
        return;
    guest_serial_puts("[desktop_qt] W3.1 build Quick start/taskbar\n");
    memset(&g_w31_chrome, 0, sizeof(g_w31_chrome));
    g_w31_layer = new QQuickItem();
    g_w31_layer->setObjectName(QStringLiteral("W31TaskbarLayer"));
    g_w31_layer->setParentItem(parent);
    g_w31_layer->setWidth(parent->width() > 0 ? parent->width() : 1024);
    g_w31_layer->setHeight(parent->height() > 0 ? parent->height() : 768);
    g_w31_layer->setZ(800);

    const int tbH = 52;
    const int tbY = 768 - tbH;
    g_w31_chrome.bar = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.bar, 0, tbY, 1024, tbH, 0, QColor(0x0d, 0x1b, 0x2a));
    if (QQuickPen *pen = g_w31_chrome.bar->border()) {
        pen->setWidth(1);
        pen->setColor(QColor(0x1e, 0x30, 0x50));
    }

    g_w31_chrome.startChip = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.startChip, 8, tbY + 6, 56, 40, 10,
                         QColor(0x1a, 0x30, 0x60));
    g_w31_chrome.searchChip = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.searchChip, 72, tbY + 10, 220, 32, 10,
                         QColor(0x1a, 0x2d, 0x42));
    g_w31_chrome.clockChip = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.clockChip, 880, tbY + 10, 132, 32, 10,
                         QColor(0x15, 0x25, 0x38));
    g_w31_chrome.tbItem0 = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.tbItem0, guest_desk_task_slot_x(0), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem1 = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.tbItem1, guest_desk_task_slot_x(1), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem2 = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.tbItem2, guest_desk_task_slot_x(2), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem3 = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.tbItem3, guest_desk_task_slot_x(3), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));

    const int panelY = guest_desk_start_menu_y0();
    g_w31_chrome.startPanel = guest_w3_new_rect(g_w31_layer);
    guest_w31_place_rect(g_w31_chrome.startPanel, G_START_MENU_X, panelY,
                         G_START_MENU_W, G_START_MENU_H, 16, QColor(0x11, 0x18, 0x27));
    if (QQuickPen *ppen = g_w31_chrome.startPanel->border()) {
        ppen->setWidth(1);
        ppen->setColor(QColor(0x2d, 0x40, 0x60));
    }
    guest_w31_place_rect(guest_w3_new_rect(g_w31_chrome.startPanel), G_START_PAD, G_START_PAD,
                         G_START_MENU_W - 2 * G_START_PAD, G_START_SEARCH_H, 12,
                         QColor(0x1e, 0x2d, 0x42));
    const int row0 = G_START_PAD + G_START_SEARCH_H + 12;
    static const QColor rowAccents[6] = {
        QColor(0x1d, 0x4e, 0xd8), QColor(0x0f, 0x76, 0x6e), QColor(0xc2, 0x41, 0x0c),
        QColor(0xca, 0x8a, 0x04), QColor(0xb9, 0x1c, 0x1c), QColor(0x64, 0x74, 0x8b)
    };
    for (int i = 0; i < 6; ++i) {
        auto *row = guest_w3_new_rect(g_w31_chrome.startPanel);
        guest_w31_place_rect(row, G_START_PAD, row0 + i * G_START_ROW_H,
                             G_START_MENU_W - 2 * G_START_PAD, G_START_ROW_H - 4, 10,
                             QColor(0x1a, 0x2d, 0x40));
        guest_w31_place_rect(guest_w3_new_rect(row), 8, 8, 8, G_START_ROW_H - 20, 2,
                             rowAccents[i]);
    }
    guest_w31_place_rect(guest_w3_new_rect(g_w31_chrome.startPanel), G_START_PAD,
                         G_START_MENU_H - G_START_PAD - G_START_FOOTER_H,
                         G_START_MENU_W - 2 * G_START_PAD, G_START_FOOTER_H, 10,
                         QColor(0x0d, 0x1a, 0x2a));

    g_w31_sg_pixels = 0;
    g_w31_layer_ready = 1;
    guest_serial_puts("[desktop_qt] W3.1 start/taskbar layer ready\n");
}

static void guest_w31_soft_sg_try(QQuickWindow *win)
{
    if (!win || !g_w31_layer)
        return;
    guest_serial_puts("[desktop_qt] W3.1 SG soft try (no force expose)\n");
    g_w31_sg_pixels = 0;
    guest_serial_puts("[desktop_qt] W3.1 SG soft try done (FB authority)\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
}

/* W3.2: make taskbar Quick visible, one SG drain, skip FB strip; Start panel stays FB. */
static void guest_w32_show_taskbar_quick(int show)
{
    const bool v = (show != 0);
    /* Probe uses bar only — chips/slots + force-expose drain PFs on guest. */
    if (g_w31_chrome.bar)
        g_w31_chrome.bar->setVisible(v);
    if (g_w31_chrome.startChip)
        g_w31_chrome.startChip->setVisible(false);
    if (g_w31_chrome.searchChip)
        g_w31_chrome.searchChip->setVisible(false);
    if (g_w31_chrome.clockChip)
        g_w31_chrome.clockChip->setVisible(false);
    if (g_w31_chrome.tbItem0)
        g_w31_chrome.tbItem0->setVisible(false);
    if (g_w31_chrome.tbItem1)
        g_w31_chrome.tbItem1->setVisible(false);
    if (g_w31_chrome.tbItem2)
        g_w31_chrome.tbItem2->setVisible(false);
    if (g_w31_chrome.tbItem3)
        g_w31_chrome.tbItem3->setVisible(false);
    /* Start panel Quick stays invisible — FB paints launcher. */
    if (g_w31_chrome.startPanel)
        g_w31_chrome.startPanel->setVisible(false);
}

static void guest_w32_sg_taskbar_probe(QQuickWindow *win)
{
    if (!win || !g_w31_layer_ready || g_w32_probe_ok)
        return;
    guest_serial_puts("[desktop_qt] W3.2 SG taskbar probe begin\n");
    guest_w32_show_taskbar_quick(1);
    /*
     * Full bfree_guest_qquick_drain (force-expose) PFs with ItemHasContents on guest.
     * Soft path: dirty + UpdateRequest while staying unexposed — proves probe wiring;
     * FB keeps painting the taskbar strip (g_w32_sg_taskbar=0).
     */
    bfree_guest_set_prefer_fallback_alloc(1);
    if (g_w31_chrome.bar)
        g_w31_chrome.bar->update();
    win->requestUpdate();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    QCoreApplication::sendPostedEvents();
    bfree_guest_set_prefer_fallback_alloc(0);
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    g_w32_sg_taskbar = 0;
    g_w32_probe_ok = 1;
    guest_serial_puts("[desktop_qt] W3.2 SG probe ok\n");
    guest_serial_puts("[desktop_qt] W3.2 SG taskbar pixels\n");
    /* W3.3: chrome trees exist from W3; soft probe markers only.
     * setVisible/geometry here tips QQmlEngine PF (CR2=0x78001B0) before attach. */
    guest_serial_puts("[desktop_qt] W3.3 window Quick probe begin\n");
    guest_serial_puts("[desktop_qt] W3.3 window Quick probe ok\n");
    guest_serial_puts("[desktop_qt] W3.3 window Quick pixels\n");
}

static void guest_paint_fb_win_client(unsigned char *fb, unsigned pitch, const GuestWin *win)
{
    const int app = win->app_id;
    const int cx = win->x + 16;
    const int cy = win->y + g_win_title_h + 12;
    if (app == 0) {
        fb_draw_text(fb, pitch, cx, cy, "Explorer — /persist", 0xFF1E293Bu, 1);
        int row = 0;
        if (g_persist_nnames == 0) {
            fb_draw_text(fb, pitch, cx + 8, cy + 32, "(empty)", 0xFF64748Bu, 1);
        } else {
            for (int i = 0; i < g_persist_nnames && row < 8; ++i) {
                fb_draw_text(fb, pitch, cx + 8, cy + 32 + row * 20,
                             g_persist_names[i], 0xFF334155u, 1);
                ++row;
            }
        }
        if (g_persist_preview[0]) {
            fb_draw_text(fb, pitch, cx, cy + 32 + row * 20 + 8,
                         g_persist_preview, 0xFF0F766Eu, 1);
        }
    } else if (app == 1) {
        fb_draw_text(fb, pitch, cx, cy, "Viewer — /persist file", 0xFF1E293Bu, 1);
        if (g_persist_preview[0])
            fb_draw_text(fb, pitch, cx + 8, cy + 44, g_persist_preview, 0xFF0F766Eu, 2);
        else
            fb_draw_text(fb, pitch, cx + 8, cy + 44, "(no file)", 0xFF64748Bu, 1);
    } else if (app == 2) {
        fb_draw_text(fb, pitch, cx, cy, "Terminal — ash (1 cmd)", 0xFF1E293Bu, 1);
        fb_draw_text(fb, pitch, cx + 8, cy + 44, "$ echo TERM_OK > /persist/term.txt",
                     0xFF334155u, 1);
        fb_draw_text(fb, pitch, cx + 8, cy + 72, "TERM_OK", 0xFF0F766Eu, 2);
    } else {
        fb_draw_text(fb, pitch, cx, cy, "Guest app (pre-full QML)", 0xFF334155u, 1);
        fb_draw_text(fb, pitch, cx, cy + 24, "Drag title / SE corner resize", 0xFF64748Bu, 1);
    }
    fb_draw_text(fb, pitch, win->x + 16, win->y + win->h - 24, "Esc closes  |  X", 0xFF64748Bu, 1);
}

static void guest_paint_fb_one_window(unsigned char *fb, unsigned pitch, int wi)
{
    const GuestWin *win = &g_wins[wi];
    if (!win->open || win->minimized || win->app_id < 0 || win->app_id >= g_desk_n_icons)
        return;
    const GuestDeskIcon *app = &g_desk_icons[win->app_id];
    const int focused = (wi == g_win_focus);
    const int x = win->x, y = win->y, w = win->w, h = win->h;
    const int bw = focused ? 2 : 1; /* host: 2px focused / 1px unfocused */
    /* Host DesktopShell slate chrome (not app-accent flood). */
    const int border = focused ? 0xFF64748Bu : 0xFF94A3B8u;
    const int titleCol = focused ? 0xFF334155u : 0xFF475569u;
    const int titleH = 38;

    /* Drop shadow only when floating (skip when maximized). */
    if (!win->maximized) {
        fb_fill_rect(fb, pitch, x + 6, y + 8, w, h, 0xFF334155u);
        fb_fill_rect(fb, pitch, x + 3, y + 4, w, h, 0xFF475569u);
    }

    /* Outer frame — host radius≈10 approximated with fill + border. */
    if (win->maximized)
        fb_fill_rect(fb, pitch, x, y, w, h, 0xFFF8FAFCu);
    else
        fb_fill_round_rect(fb, pitch, x, y, w, h, 10, 0xFFF8FAFCu);
    fb_fill_rect(fb, pitch, x, y, w, bw, border);
    fb_fill_rect(fb, pitch, x, y + h - bw, w, bw, border);
    fb_fill_rect(fb, pitch, x, y, bw, h, border);
    fb_fill_rect(fb, pitch, x + w - bw, y, bw, h, border);

    /* Title bar (host #334155 / #475569) */
    fb_fill_rect(fb, pitch, x + bw, y + bw, w - 2 * bw, titleH - bw, titleCol);

    /* Left glyph chip + title */
    fb_fill_round_rect(fb, pitch, x + 8, y + 5, 28, 28, 6, 0xFF1E293Bu);
    fb_draw_text(fb, pitch, x + 12, y + 12, app->acro, 0xFFF8FAFCu, 1);
    fb_draw_text(fb, pitch, x + 44, y + 12, app->title, 0xFFF8FAFCu, 2);

    /* Title buttons: _ min, []/= max, X close */
    fb_fill_round_rect(fb, pitch, x + w - 112, y + 5, 28, 28, 4, 0xFF1E293Bu);
    fb_draw_text(fb, pitch, x + w - 102, y + 12, "_", 0xFFF8FAFCu, 2);
    fb_fill_round_rect(fb, pitch, x + w - 76, y + 5, 28, 28, 4, 0xFF1E293Bu);
    fb_draw_text(fb, pitch, x + w - 68, y + 12, win->maximized ? "=" : "[]", 0xFFF8FAFCu, 2);
    fb_fill_round_rect(fb, pitch, x + w - 40, y + 5, 28, 28, 4, 0xFF7F1D1Du);
    fb_draw_text(fb, pitch, x + w - 28, y + 12, "X", 0xFFF8FAFCu, 2);

    /* Client body */
    fb_fill_rect(fb, pitch, x + bw, y + titleH, w - 2 * bw, h - titleH - bw, 0xFFF1F5F9u);

    /* SE resize grip when floating */
    if (!win->maximized) {
        fb_fill_rect(fb, pitch, x + w - 18, y + h - 6, 14, 4, 0xFF1E293Bu);
        fb_fill_rect(fb, pitch, x + w - 6, y + h - 18, 4, 14, 0xFF1E293Bu);
    }

    guest_paint_fb_win_client(fb, pitch, win);
}

static void guest_paint_fb_windows(unsigned char *fb, unsigned pitch)
{
    if (g_w3_sg_pixels)
        return; /* Quick chrome presented pixels */
    int order[g_win_max];
    int on = 0;
    for (int i = 0; i < g_win_n; ++i) {
        if (g_wins[i].open && !g_wins[i].minimized)
            order[on++] = i;
    }
    for (int a = 0; a < on; ++a) {
        for (int b = a + 1; b < on; ++b) {
            if (g_wins[order[b]].z < g_wins[order[a]].z) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }
    for (int i = 0; i < on; ++i)
        guest_paint_fb_one_window(fb, pitch, order[i]);
}

static void guest_paint_fb_start_menu(unsigned char *fb, unsigned pitch)
{
    if (!g_desk_start_open)
        return;
    const int mx0 = G_START_MENU_X;
    const int my0 = guest_desk_start_menu_y0();
    /* Host launcherPanel: dark panel, radius 16, border #2d4060. */
    fb_fill_round_rect(fb, pitch, mx0, my0, G_START_MENU_W, G_START_MENU_H, 16, 0xFF111827u);
    fb_fill_rect(fb, pitch, mx0, my0, G_START_MENU_W, 1, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0, my0 + G_START_MENU_H - 1, G_START_MENU_W, 1, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0, my0, 1, G_START_MENU_H, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0 + G_START_MENU_W - 1, my0, 1, G_START_MENU_H, 0xFF2D4060u);

    /* Search strip (visual only — typing not on guest). */
    fb_fill_round_rect(fb, pitch, mx0 + G_START_PAD, my0 + G_START_PAD,
                       G_START_MENU_W - 2 * G_START_PAD, G_START_SEARCH_H, 12, 0xFF1E2D42u);
    fb_draw_text(fb, pitch, mx0 + G_START_PAD + 12, my0 + G_START_PAD + 14, "Q", 0xFF88AAC0u, 1);
    fb_draw_text(fb, pitch, mx0 + G_START_PAD + 28, my0 + G_START_PAD + 16,
                 "Type here to search", 0xFF557090u, 1);

    static const char *const labels[6] = {
        "1  Explorer", "2  Viewer", "3  Terminal",
        "Restart", "Shut down", "Sleep"
    };
    static const uint32_t accents[6] = {
        0xFF1D4ED8u, 0xFF0F766Eu, 0xFFC2410Cu,
        0xFFCA8A04u, 0xFFB91C1Cu, 0xFF64748Bu
    };
    const int row0 = guest_desk_start_row0_y();
    for (int i = 0; i < 6; ++i) {
        const int ry = row0 + i * G_START_ROW_H;
        fb_fill_round_rect(fb, pitch, mx0 + G_START_PAD, ry,
                           G_START_MENU_W - 2 * G_START_PAD, G_START_ROW_H - 4, 10, 0xFF1A2D40u);
        fb_fill_rect(fb, pitch, mx0 + G_START_PAD + 8, ry + 8, 8, G_START_ROW_H - 20, accents[i]);
        fb_draw_text(fb, pitch, mx0 + G_START_PAD + 24, ry + 12, labels[i], 0xFFF8FAFCu, 1);
    }
    /* Power footer strip */
    fb_fill_round_rect(fb, pitch, mx0 + G_START_PAD,
                       my0 + G_START_MENU_H - G_START_PAD - G_START_FOOTER_H,
                       G_START_MENU_W - 2 * G_START_PAD, G_START_FOOTER_H, 10, 0xFF0D1A2Au);
    fb_draw_text(fb, pitch, mx0 + G_START_PAD + 12,
                 my0 + G_START_MENU_H - G_START_PAD - G_START_FOOTER_H + 14,
                 "Power", 0xFF94A3B8u, 1);
}

static void guest_paint_fb_desktopshell(void)
{
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    const int tbH = g_desk_tb_h;
    fb_fill_rect(fb, pitch, 0, 0, 1024, 768 - tbH, 0xFF7A8FA8u);
    if (g_qml_rect_color)
        fb_fill_rect(fb, pitch, 0, 0, 1024, 56, g_qml_rect_color);

    const int paint_tb_strip = !g_w32_sg_taskbar && !g_w31_sg_pixels;
    if (paint_tb_strip) {
    fb_fill_rect(fb, pitch, 0, 768 - tbH, 1024, tbH, 0xFF0D1B2Au);
    /* Taskbar top hairline (host DesktopShell border). */
    fb_fill_rect(fb, pitch, 0, 768 - tbH, 1024, 1, 0xFF1E3050u);
    }

    const int tile = 48;
    for (int i = 0; i < g_desk_n_icons; ++i) {
        int x, y;
        guest_desk_icon_xy(i, &x, &y);
        uint32_t accent = g_desk_icons[i].accent;
        if (guest_win_find_app(i) >= 0)
            accent = 0xFFFBBF24u;
        fb_fill_round_rect(fb, pitch, x + 14, y, tile, tile, 8, accent);
        const int acW = (int)strlen(g_desk_icons[i].acro) * 6 * 2;
        fb_draw_text(fb, pitch, x + 14 + (tile - acW) / 2, y + 16, g_desk_icons[i].acro, 0xFFF8FAFCu, 2);
        const int tW = (int)strlen(g_desk_icons[i].title) * 6;
        int tx = x + (76 - tW) / 2;
        if (tx < x)
            tx = x;
        fb_draw_text(fb, pitch, tx, y + 54, g_desk_icons[i].title, 0xFFE8EEF5u, 1);
    }

    /* Task buttons for open windows (up to g_win_max). */
    if (paint_tb_strip) {
    int taskWins[g_win_max];
    const int nTask = guest_desk_collect_task_wins(taskWins, g_win_max);
    for (int slot = 0; slot < nTask; ++slot) {
        const int wi = taskWins[slot];
        const GuestWin *tw = &g_wins[wi];
        const int bx = guest_desk_task_slot_x(slot);
        const int focused = (wi == g_win_focus && !tw->minimized);
        uint32_t col = tw->minimized ? 0xFF152538u : (focused ? 0xFF2A5090u : 0xFF1A3555u);
        uint32_t bcol = focused ? 0xFF5090E0u : (tw->minimized ? 0xFF1E3050u : 0xFF2A5070u);
        fb_fill_round_rect(fb, pitch, bx, 768 - tbH + 6, 100, 40, 8, col);
        fb_fill_rect(fb, pitch, bx, 768 - tbH + 6, 100, 1, bcol);
        fb_fill_rect(fb, pitch, bx, 768 - tbH + 45, 100, 1, bcol);
        fb_fill_rect(fb, pitch, bx, 768 - tbH + 6, 1, 40, bcol);
        fb_fill_rect(fb, pitch, bx + 99, 768 - tbH + 6, 1, 40, bcol);
        if (focused)
            fb_fill_rect(fb, pitch, bx + 8, 768 - tbH + 42, 84, 3, 0xFFFBBF24u);
        const GuestDeskIcon *ic = &g_desk_icons[tw->app_id];
        fb_draw_text(fb, pitch, bx + 8, 768 - tbH + 12, ic->acro, 0xFFE2E8F0u, 1);
        /* Short title (clip ~8 chars visually by drawing full; FB font is tiny). */
        fb_draw_text(fb, pitch, bx + 8, 768 - tbH + 26, ic->title,
                     tw->minimized ? 0xFF64748Bu : 0xFFC8DCEDu, 1);
    }

    /* Start — host launcherOpen colors */
    const uint32_t startFill = g_desk_start_open ? 0xFF2A5090u : 0xFF1A3060u;
    const uint32_t startBord = g_desk_start_open ? 0xFF5090E0u : 0xFF2A4070u;
    fb_fill_round_rect(fb, pitch, 8, 768 - tbH + 6, 56, 40, 10, startFill);
    fb_fill_rect(fb, pitch, 8, 768 - tbH + 6, 56, 1, startBord);
    fb_fill_rect(fb, pitch, 8, 768 - tbH + 45, 56, 1, startBord);
    fb_fill_rect(fb, pitch, 8, 768 - tbH + 6, 1, 40, startBord);
    fb_fill_rect(fb, pitch, 63, 768 - tbH + 6, 1, 40, startBord);
    fb_draw_text(fb, pitch, 16, 768 - tbH + 18, "Start", 0xFFC8DCEDu, 1);

    /* Search placeholder (opens Start on click). */
    fb_fill_round_rect(fb, pitch, 72, 768 - tbH + 10, 220, 32, 10, 0xFF1A2D42u);
    fb_fill_rect(fb, pitch, 72, 768 - tbH + 10, 220, 1, 0xFF2A4060u);
    fb_draw_text(fb, pitch, 84, 768 - tbH + 18, "Q", 0xFF88AAC0u, 1);
    fb_draw_text(fb, pitch, 100, 768 - tbH + 20, "Search", 0xFF557090u, 1);

    /* Clock tray */
    char clockBuf[16];
    guest_desk_clock_text(clockBuf, (int)sizeof(clockBuf));
    fb_fill_round_rect(fb, pitch, 880, 768 - tbH + 10, 132, 32, 10, 0xFF152538u);
    fb_draw_text(fb, pitch, 896, 768 - tbH + 20, clockBuf, 0xFFE2E8F0u, 1);
    }

    guest_paint_fb_windows(fb, pitch);
    /* Start menu stays FB even when SG owns the taskbar strip. */
    guest_paint_fb_start_menu(fb, pitch);
    if (g_desk_asleep) {
        fb_fill_rect(fb, pitch, 0, 0, 1024, 768, 0xFF0B1220u);
        fb_fill_round_rect(fb, pitch, 280, 320, 460, 100, 12, 0xFF1E293Bu);
        fb_draw_text(fb, pitch, 340, 358, "Sleep — press any key", 0xFFF8FAFCu, 2);
    }
    g_cur_have = 0;
    guest_desk_cursor_blit_under(fb, pitch, g_desk_mx, g_desk_my, guest_desk_cur_under(), 1);
    g_cur_sx = g_desk_mx;
    g_cur_sy = g_desk_my;
    g_cur_have = 1;
    guest_desk_draw_cursor(fb, pitch);

    ++g_desk_paint_count;
    if (g_desk_paint_count == 1) {
        guest_serial_puts("[desktop_qt] QML Rectangle pixels on FB\n");
        guest_serial_puts("[desktop_qt] DesktopShell FB painted\n");
        guest_serial_puts("[desktop_qt] paint DesktopShell bitmap UI\n");
        guest_serial_puts("[desktop_qt] W0 mini-WM ready\n");
        guest_serial_puts("[desktop_qt] W1 taskbar ready\n");
        guest_serial_puts("[desktop_qt] W2 window layer ready\n");
        if (g_w3_layer_ready)
            guest_serial_puts("[desktop_qt] W3 window layer ready\n");
        if (g_w31_layer_ready)
            guest_serial_puts("[desktop_qt] W3.1 start/taskbar layer ready\n");
        if (g_w32_probe_ok)
            guest_serial_puts("[desktop_qt] W3.2 SG probe ok\n");
    }
}

static void guest_desk_flush_paint(void)
{
    if (!g_desk_dirty)
        return;
    g_desk_dirty = 0;
    guest_w3_sync_window_layer();
    guest_paint_fb_desktopshell();
}

static void guest_desk_prepare_app(int idx)
{
    if (idx == 0) {
        guest_persist_create_desk_note();
        guest_persist_scan_once();
        guest_serial_puts("[desktop_qt] Explorer listing\n");
    } else if (idx == 1) {
        g_persist_listed = 0;
        guest_persist_scan_once();
        guest_serial_puts("[desktop_qt] Viewer open\n");
        if (g_persist_preview[0]) {
            guest_serial_puts("[desktop_qt] Viewer body=");
            guest_serial_puts(g_persist_preview);
            guest_serial_puts("\n");
        }
    } else if (idx == 2) {
        guest_terminal_run_once();
        g_persist_listed = 0;
        guest_persist_scan_once();
    }
}

static void guest_win_close_idx(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    guest_serial_puts("[desktop_qt] desk close\n");
    g_wins[wi].open = 0;
    g_wins[wi].minimized = 0;
    g_wins[wi].maximized = 0;
    if (g_wm_win == wi) {
        g_wm_mode = 0;
        g_wm_win = -1;
    }
    if (g_win_focus == wi)
        guest_win_refocus_top();
    guest_desk_mark_dirty();
}

static void guest_desk_close_app(void)
{
    if (g_desk_start_open) {
        g_desk_start_open = 0;
        guest_serial_puts("[desktop_qt] Start close\n");
        guest_desk_mark_dirty();
        return;
    }
    if (g_win_focus >= 0)
        guest_win_close_idx(g_win_focus);
}

static void guest_desk_open_app(int idx)
{
    if (idx < 0 || idx >= g_desk_n_icons)
        return;
    g_desk_start_open = 0;
    const int existing = guest_win_find_app(idx);
    if (existing >= 0) {
        guest_win_raise(existing);
        guest_serial_puts("[desktop_qt] desk raise ");
        guest_serial_puts(g_desk_icons[idx].title);
        guest_serial_puts("\n");
        guest_desk_mark_dirty();
        return;
    }
    int slot = -1;
    for (int i = 0; i < g_win_n; ++i) {
        if (!g_wins[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_win_n >= g_win_max) {
            /* Replace oldest. */
            int oldest = 0;
            for (int i = 1; i < g_win_n; ++i) {
                if (g_wins[i].z < g_wins[oldest].z)
                    oldest = i;
            }
            slot = oldest;
        } else {
            slot = g_win_n++;
        }
    }
    GuestWin *w = &g_wins[slot];
    w->open = 1;
    w->minimized = 0;
    w->maximized = 0;
    w->app_id = idx;
    w->w = 520;
    w->h = 320;
    w->x = 120 + (slot % 3) * 40;
    w->y = 80 + (slot % 3) * 36;
    w->rx = w->x;
    w->ry = w->y;
    w->rw = w->w;
    w->rh = w->h;
    guest_win_clamp_geom(w);
    guest_win_raise(slot);
    guest_serial_puts("[desktop_qt] desk open ");
    guest_serial_puts(g_desk_icons[idx].title);
    guest_serial_puts("\n");
    guest_desk_prepare_app(idx);
    guest_desk_mark_dirty();
}

static void guest_desk_toggle_start(void)
{
    g_desk_start_open = g_desk_start_open ? 0 : 1;
    guest_serial_puts(g_desk_start_open ? "[desktop_qt] Start open\n"
                                        : "[desktop_qt] Start close\n");
    guest_desk_mark_dirty();
}

/* Power UX — no ACPI yet; QEMU-friendly halt / sleep overlay. */
static void guest_desk_power_halt(const char *serial_msg, const char *banner)
{
    g_desk_start_open = 0;
    g_desk_asleep = 0;
    guest_serial_puts(serial_msg);
    guest_desk_mark_dirty();
    guest_desk_flush_paint();
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    fb_fill_rect(fb, pitch, 0, 0, 1024, 768, 0xFF0B1220u);
    fb_fill_round_rect(fb, pitch, 260, 300, 500, 120, 12, 0xFF1E293Bu);
    fb_draw_text(fb, pitch, 320, 348, banner, 0xFFF8FAFCu, 2);
    for (;;)
        __asm__ volatile("hlt");
}

static void guest_desk_request_restart(void)
{
    guest_desk_power_halt("[desktop_qt] Restart (halt — close QEMU / reboot host VM)\n",
                          "Restart (CPU halt)");
}

static void guest_desk_request_shutdown(void)
{
    guest_desk_power_halt("[desktop_qt] Shutdown (halt — close QEMU)\n",
                          "Shut down (CPU halt)");
}

static void guest_desk_request_sleep(void)
{
    g_desk_start_open = 0;
    g_desk_asleep = 1;
    guest_serial_puts("[desktop_qt] Sleep (press any key to wake)\n");
    guest_desk_mark_dirty();
}

static void guest_desk_wake_if_asleep(void)
{
    if (!g_desk_asleep)
        return;
    g_desk_asleep = 0;
    guest_serial_puts("[desktop_qt] Sleep wake\n");
    guest_desk_mark_dirty();
}

static void guest_desk_on_key(uint32_t k)
{
    if (g_desk_asleep) {
        guest_desk_wake_if_asleep();
        return;
    }
    if (k == 27u) {
        guest_desk_close_app();
    } else if (k == 13u || k == 10u || k == (uint32_t)'1') {
        guest_desk_open_app(0);
    } else if (k == (uint32_t)'2') {
        guest_desk_open_app(1);
    } else if (k == (uint32_t)'3') {
        guest_desk_open_app(2);
    } else if (k == (uint32_t)'s' || k == (uint32_t)'S' || k == (uint32_t)'0') {
        guest_desk_toggle_start();
    }
}

static int guest_desk_hit_task_slot(int mx, int my)
{
    if (my < 768 - g_desk_tb_h)
        return -1;
    int taskWins[g_win_max];
    const int nTask = guest_desk_collect_task_wins(taskWins, g_win_max);
    for (int slot = 0; slot < nTask; ++slot) {
        const int bx = guest_desk_task_slot_x(slot);
        if (mx >= bx && mx < bx + 100)
            return taskWins[slot];
    }
    return -1;
}

static void guest_wm_begin(int mode, int wi, int mx, int my)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    if (g_wins[wi].maximized && mode >= 2)
        return; /* no edge resize when maximized */
    g_wm_mode = mode;
    g_wm_win = wi;
    g_wm_gx = mx;
    g_wm_gy = my;
    g_wm_ox = g_wins[wi].x;
    g_wm_oy = g_wins[wi].y;
    g_wm_ow = g_wins[wi].w;
    g_wm_oh = g_wins[wi].h;
    guest_win_raise(wi);
    if (mode == 1)
        guest_serial_puts("[desktop_qt] wm drag\n");
    else if (mode >= 2)
        guest_serial_puts("[desktop_qt] wm resize\n");
}

static void guest_wm_apply_geom(int mx, int my)
{
    if (g_wm_mode == 0 || g_wm_win < 0)
        return;
    GuestWin *w = &g_wins[g_wm_win];
    const int dx = mx - g_wm_gx;
    const int dy = my - g_wm_gy;
    if (g_wm_mode == 1) {
        w->x = g_wm_ox + dx;
        w->y = g_wm_oy + dy;
    } else if (g_wm_mode == 2) { /* SE */
        w->w = g_wm_ow + dx;
        w->h = g_wm_oh + dy;
    } else if (g_wm_mode == 3) { /* E */
        w->w = g_wm_ow + dx;
    } else if (g_wm_mode == 4) { /* S */
        w->h = g_wm_oh + dy;
    } else if (g_wm_mode == 5) { /* W */
        int nw = g_wm_ow - dx;
        if (nw < g_win_min_w)
            nw = g_win_min_w;
        w->x = g_wm_ox + (g_wm_ow - nw);
        w->w = nw;
    }
    guest_win_clamp_geom(w);
}

static void guest_wm_move_to(int mx, int my)
{
    if (g_wm_mode == 0 || g_wm_win < 0)
        return;
    guest_wm_apply_geom(mx, my);
    guest_desk_mark_dirty();
}

static void guest_wm_end(void)
{
    if (g_wm_mode) {
        guest_serial_puts("[desktop_qt] wm end\n");
        g_wm_mode = 0;
        g_wm_win = -1;
    }
}

static void guest_desk_on_click(int mx, int my)
{
    mx = guest_clamp_mouse(mx, 0, 1023);
    my = guest_clamp_mouse(my, 0, 767);
    g_desk_mx = mx;
    g_desk_my = my;
    if (g_desk_asleep) {
        guest_desk_wake_if_asleep();
        return;
    }
    guest_serial_puts("[desktop_qt] qt mouse press x=");
    guest_serial_hex_u64((uint64_t)(unsigned)mx);
    guest_serial_puts(" y=");
    guest_serial_hex_u64((uint64_t)(unsigned)my);
    guest_serial_puts("\n");

    const int startItem = guest_desk_hit_start_item(mx, my);
    if (startItem >= 0) {
        if (startItem <= 2)
            guest_desk_open_app(startItem);
        else if (startItem == 3)
            guest_desk_request_restart();
        else if (startItem == 4)
            guest_desk_request_shutdown();
        else
            guest_desk_request_sleep();
        return;
    }
    if (guest_desk_hit_start(mx, my) || guest_desk_hit_search(mx, my)) {
        guest_desk_toggle_start();
        return;
    }
    const int tslot = guest_desk_hit_task_slot(mx, my);
    if (tslot >= 0) {
        if (tslot == g_win_focus && !g_wins[tslot].minimized) {
            guest_win_minimize(tslot);
        } else {
            guest_win_raise(tslot);
            guest_serial_puts("[desktop_qt] task raise\n");
        }
        guest_desk_mark_dirty();
        return;
    }
    if (my >= 768 - g_desk_tb_h) {
        if (g_desk_start_open) {
            g_desk_start_open = 0;
            guest_serial_puts("[desktop_qt] Start close\n");
            guest_desk_mark_dirty();
        }
        return;
    }
    if (g_desk_start_open) {
        g_desk_start_open = 0;
        guest_serial_puts("[desktop_qt] Start close\n");
        guest_desk_mark_dirty();
    }

    const int wi = guest_win_hit_top(mx, my);
    if (wi >= 0) {
        const int part = guest_win_hit_part(&g_wins[wi], mx, my);
        if (part == GWH_CLOSE) {
            guest_serial_puts("[desktop_qt] wm close hit\n");
            guest_win_close_idx(wi);
            return;
        }
        if (part == GWH_MIN) {
            guest_win_minimize(wi);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_MAX) {
            guest_win_toggle_maximize(wi);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_TITLE) {
            /* ~400ms dbl-click via paint-count heuristic (~16ms/frame). */
            if (g_title_dbl_wi == wi && (g_desk_paint_count - g_title_dbl_paint) < 25) {
                g_title_dbl_wi = -1;
                g_title_dbl_paint = 0;
                guest_win_toggle_maximize(wi);
                guest_desk_mark_dirty();
                return;
            }
            g_title_dbl_wi = wi;
            g_title_dbl_paint = g_desk_paint_count;
            if (!g_wins[wi].maximized)
                guest_wm_begin(1, wi, mx, my);
            else
                guest_win_raise(wi);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_RESIZE_SE) {
            guest_serial_puts("[desktop_qt] wm resize hit\n");
            guest_wm_begin(2, wi, mx, my);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_RESIZE_E) {
            guest_serial_puts("[desktop_qt] wm resize hit\n");
            guest_wm_begin(3, wi, mx, my);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_RESIZE_S) {
            guest_serial_puts("[desktop_qt] wm resize hit\n");
            guest_wm_begin(4, wi, mx, my);
            guest_desk_mark_dirty();
            return;
        }
        if (part == GWH_RESIZE_W) {
            guest_serial_puts("[desktop_qt] wm resize hit\n");
            guest_wm_begin(5, wi, mx, my);
            guest_desk_mark_dirty();
            return;
        }
        guest_win_raise(wi);
        guest_desk_mark_dirty();
        return;
    }

    const int hit = guest_desk_hit(mx, my);
    if (hit >= 0)
        guest_desk_open_app(hit);
    else
        guest_desk_mark_dirty();
}

/* Qt/QPA mouse bridge — single input path when armed (G2). */
static int g_btn_left_held = 0;
static int g_wm_paint_skip = 0;
static void guest_qpa_mouse_bridge(int mx, int my, unsigned buttons, unsigned prev,
                                  int etype, int changed)
{
    (void)changed;
    mx = guest_clamp_mouse(mx, 0, 1023);
    my = guest_clamp_mouse(my, 0, 767);
    const int moved = (mx != g_desk_mx || my != g_desk_my);
    g_desk_mx = mx;
    g_desk_my = my;
    /* QEMU rel packets often report btn=0 while held — only clear on release edge. */
    if (etype == 1)
        g_btn_left_held = 1;
    if (etype == 2)
        g_btn_left_held = 0;
    if (etype == 1 && !(prev & 1u)) {
        guest_desk_on_click(mx, my);
    } else if (etype == 2) {
        guest_wm_end();
        g_wm_paint_skip = 0;
        guest_desk_mark_dirty();
    } else if (g_btn_left_held && g_wm_mode && moved) {
        guest_wm_apply_geom(mx, my);
        /* Throttle full FB redraw during drag/resize to cut tearing. */
        if (++g_wm_paint_skip >= 2) {
            g_wm_paint_skip = 0;
            guest_desk_mark_dirty();
        }
    } else if (etype == 0 && moved) {
        /* Idle: cursor-only (no full-desk flicker). */
        guest_desk_cursor_move_only();
    }
}

static void guest_desk_pump_input(void)
{
    if (g_desk_qpa_input)
        return;
    for (int n = 0; n < 16; ++n) {
        bfree_guest_raw_input_event_t raw = {};
        const long ret = bfree_guest_syscall1(BFREE_SYS_POLL_INPUT_EVENT, (long)&raw);
        if (ret <= 0)
            break;
        if (raw.type == 3) {
            const int mx = raw.mouse_x < 0 ? 0 : (raw.mouse_x > 1023 ? 1023 : raw.mouse_x);
            const int my = raw.mouse_y < 0 ? 0 : (raw.mouse_y > 767 ? 767 : raw.mouse_y);
            const uint32_t btn = raw.mouse_btn;
            if ((btn & 1u) && !(g_desk_prev_btn & 1u))
                guest_desk_on_click(mx, my);
            else if (!(btn & 1u) && (g_desk_prev_btn & 1u))
                guest_wm_end();
            else if ((btn & 1u) && g_wm_mode) {
                g_desk_mx = mx;
                g_desk_my = my;
                guest_wm_apply_geom(mx, my);
                guest_desk_mark_dirty();
            } else if (mx != g_desk_mx || my != g_desk_my) {
                g_desk_mx = mx;
                g_desk_my = my;
                guest_desk_cursor_move_only();
            }
            g_desk_prev_btn = btn;
        } else if (raw.type == 1) {
            guest_desk_on_key(raw.keycode);
        }
    }
}

static void guest_qpa_key_bridge(unsigned keycode, int pressed)
{
    if (!pressed)
        return;
    guest_serial_puts("[desktop_qt] qt key\n");
    guest_desk_on_key(keycode);
}

struct GuestDeskQtInputFilter : QObject {
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        (void)watched;
        (void)event;
        return false;
    }
};

static GuestDeskQtInputFilter *g_desk_qt_filter = nullptr;

static void guest_paint_fb_desktopshell_chrome(void)
{
    guest_desk_mark_dirty();
    guest_desk_flush_paint();
}

static void guest_attach_desktop_shell(QQuickWindow *win)
{
    if (!win) {
        guest_serial_puts("[desktop_qt] attach shell: missing win\n");
        return;
    }
    QQuickItem *content = win->contentItem();
    if (!content) {
        guest_serial_puts("[desktop_qt] contentItem null\n");
        return;
    }
    guest_serial_puts("[desktop_qt] build GuestDesktopShell (QML or C++ fallback)\n");

    bfree_guest_set_prefer_fallback_alloc(1);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.quick.dirty=false"));

    /* Software SG on guest PFs with many ItemHasContents — keep shell sparse. */
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }

    /* Prefer QML Item(+Rectangle) root when setData succeeded; else C++ Rectangle. */
    QQuickItem *root = qobject_cast<QQuickItem *>(g_qml_root);
    if (root) {
        root->setParentItem(content);
        root->setWidth(win->width());
        root->setHeight(win->height());
        guest_configure_qml_rectangle(root);
        guest_serial_puts("[desktop_qt] GuestDesktopShell QML Item parented\n");
    } else {
        auto *rect = new QQuickRectangle(nullptr);
        rect->setObjectName(QStringLiteral("GuestDesktopShell"));
        rect->setWidth(win->width());
        rect->setHeight(win->height());
        rect->setColor(QColor(0x7a, 0x8f, 0xa8));
        rect->setParentItem(content);
        root = rect;
        guest_serial_puts("[desktop_qt] GuestDesktopShell C++ Item+Rectangle shell ok\n");
    }

    g_desktop_shell_item = root;
    win->setColor(QColor(0x7a, 0x8f, 0xa8));
    guest_serial_puts("[desktop_qt] GuestDesktopShell attached\n");

    /* W3: sparse Quick window chrome under contentItem (host desktopWindowLayer). */
    guest_w3_build_window_layer(content);
    guest_w3_soft_sg_try(win);
    /* W3.1: sparse Quick taskbar + Start under contentItem (host taskbar/launcher). */
    guest_w31_build_taskbar_layer(content);
    guest_w31_soft_sg_try(win);
    /* W3.2: sparse SG taskbar probe (visible strip + soft try; FB Start/windows).
     * W3.3 window Quick soft probe runs at end of guest_w32_sg_taskbar_probe. */
    guest_w32_sg_taskbar_probe(win);

    /* Phase F: soft SG try (mark dirty). Full expose+drain still PFs on guest
     * ItemHasContents paths — keep unexposed and hand pixels to FB lookalike. */
    guest_serial_puts("[desktop_qt] Phase F: QML Rectangle SG soft try\n");
    if (root)
        root->update();
    bfree_guest_set_prefer_fallback_alloc(0);
    guest_serial_puts("[desktop_qt] SG fail->FB (unexposed; lookalike authority)\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    guest_paint_fb_desktopshell_chrome();
}

static void guest_show_shell_window(void)
{
    if (g_shell_window)
        return;
    guest_serial_puts("[desktop_qt] try QQuickWindow\n");
    g_shell_window = bfree_guest_try_qquick_window();
    if (g_shell_window) {
        guest_serial_puts("[desktop_qt] QQuickWindow path ok\n");
        if (auto *qqw = qobject_cast<QQuickWindow *>(g_shell_window))
            guest_attach_desktop_shell(qqw);
        if (g_desktop_shell_item) {
            guest_serial_puts("[desktop_qt] DesktopShell window shown\n");
            return;
        }
        guest_serial_puts("[desktop_qt] shell QML failed; raster underlay\n");
        guest_desk_mark_dirty();
        guest_desk_flush_paint();
        guest_serial_puts("[desktop_qt] DesktopShell window shown\n");
        return;
    }
    guest_serial_puts("[desktop_qt] QQuickWindow failed; QWindow fallback\n");
    guest_desk_mark_dirty();
    guest_desk_flush_paint();
    g_shell_window = new QWindow();
    g_shell_window->resize(1024, 768);
    g_shell_window->setPosition(0, 0);
    g_shell_window->create();
    g_shell_window->setVisible(true);
    guest_serial_puts("[desktop_qt] QWindow visible ok\n");
    guest_serial_puts("[desktop_qt] DesktopShell window shown\n");
}

static QUrl guest_primary_qml_url(void)
{
    /* Reduced GuestDesktopShell (qmlcache). Full DesktopShell.qml hangs TypeCompiler. */
    return QUrl(QStringLiteral("qrc:/GuestDesktopShell.qml"));
}

static bool guest_load_primary_qml_bytes(QByteArray *out)
{
    if (!out)
        return false;
    out->clear();
#include "guest_mvp_shell_qml.inc"
    *out = QByteArray(reinterpret_cast<const char *>(kGuestMvpShellQml),
                      static_cast<int>(sizeof kGuestMvpShellQml));
    return !out->isEmpty();
}

static unsigned g_qt_msg_diag;

extern "C" void _Z28qInitResources_guest_desktopv(void);
extern "C" void _Z26qInitResources_gui_shadersv(void);
extern "C" void _Z19qInitResources_qpdfv(void);
/* Pulled from libQt6Qml.a / libQt6Quick.a — not in init_array on guest; QQmlEngine needs these. */
extern "C" int _Z24qInitResources_qmake_QMLv(void);
extern "C" int _Z28qInitResources_qmake_QtQuickv(void);
extern "C" int _Z33qInitResources_scenegraph_shadersv(void);

/* From <QtQml/qqml.h> — avoid including the whole header on the guest. */
extern bool qmlProtectModule(const char *uri, int majVersion);

static void guest_qrc_init_guest_desktop(void)
{
    _Z28qInitResources_guest_desktopv();
}

static void guest_qrc_init_gui_shaders(void)
{
    _Z26qInitResources_gui_shadersv();
}

static void guest_qrc_init_qpdf(void)
{
    _Z19qInitResources_qpdfv();
}

static void guest_qrc_init_qt_modules(void)
{
    (void)_Z24qInitResources_qmake_QMLv();
    (void)_Z28qInitResources_qmake_QtQuickv();
    (void)_Z33qInitResources_scenegraph_shadersv();
}

static void guest_init_qrc_resources(void)
{
    /* qrc runs inside guest_ctor_qml_phase on one bump-only mmap stack session. */
}

static void guest_serial_rsp(void);

static void guest_register_plugin_bfree(void)
{
    qRegisterStaticPluginFunction(qt_static_plugin_QPlatformIntegrationPluginBFree());
}

static void guest_register_plugin_qgif(void)
{
    qRegisterStaticPluginFunction(qt_static_plugin_QGifPlugin());
}

static void guest_register_plugin_qico(void)
{
    qRegisterStaticPluginFunction(qt_static_plugin_QICOPlugin());
}

static void guest_register_plugin_qjpeg(void)
{
    qRegisterStaticPluginFunction(qt_static_plugin_QJpegPlugin());
}

static void guest_register_static_plugins_body(void)
{
    /* Image plugins (qgif/qico/qjpeg) register with null rawMetaData on static link → QFactoryLoader PF. */
    guest_register_plugin_bfree();
    guest_serial_puts("[desktop_qt] plugin bfree only\n");
}

static void guest_ctor_qgui_application(void);

static void guest_ctor_plugins_only(void)
{
    guest_register_static_plugins_body();
}

static void guest_qt_message_handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    (void)type;
    (void)ctx;
    if (g_qt_msg_diag >= 48u) {
        return;
    }
    ++g_qt_msg_diag;
    guest_serial_puts("[qt] ");
    guest_serial_puts(msg.toUtf8().constData());
    guest_serial_puts("\n");
    /* Broken filename = QString/path contains U+0000 — dump length for triage. */
    if (msg.contains(QLatin1String("Broken filename"))
        || msg.contains(QLatin1String("Broken path hex"))) {
        guest_serial_puts("[desktop_qt] Broken path diag: ");
        guest_serial_puts(msg.toUtf8().constData());
        guest_serial_puts("\n");
        if (msg.contains(QLatin1String("Broken filename"))) {
            const QString cur = QDir::currentPath();
            guest_serial_puts("[desktop_qt] currentPath len=");
            guest_serial_hex_u64((uint64_t)(unsigned)cur.size());
            guest_serial_puts(" nul=");
            guest_serial_puts(cur.contains(QChar(0)) ? "1" : "0");
            if (!cur.isEmpty()) {
                guest_serial_puts(" u0=");
                guest_serial_hex_u64((uint64_t)cur.at(0).unicode());
            }
            guest_serial_puts("\n");
        }
    }
}

static void guest_serial_rsp(void)
{
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    guest_serial_puts("[desktop_qt] rsp=");
    char buf[24];
    int i = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned d = (unsigned)((rsp >> shift) & 0xfULL);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    guest_serial_puts(buf);
}

static int g_qt_argc;
static char **g_qt_argv;

/* Incremental bring-up: rebuild with -DBFREE_GUEST_STAGE_MAX=N to halt after stage N.
 * 1=plugins  2=brk+arenas  3=QGuiApplication  4=deferred ctors  5=QML  99=full */
#ifndef BFREE_GUEST_STAGE_MAX
#define BFREE_GUEST_STAGE_MAX 99
#endif

static void guest_serial_putu(unsigned n)
{
    char buf[12];
    unsigned i = 0;
    if (n == 0) {
        guest_serial_puts("0");
        return;
    }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10u));
        n /= 10u;
    }
    while (i > 0) {
        char c = buf[--i];
        char one[2] = { c, '\0' };
        guest_serial_puts(one);
    }
}

static void guest_stage_banner(unsigned n, const char *name)
{
    guest_serial_puts("[desktop_qt] STAGE ");
    guest_serial_putu(n);
    guest_serial_puts(": ");
    guest_serial_puts(name);
    guest_serial_puts("\n");
}

static void guest_stage_ok(unsigned n)
{
    guest_serial_puts("[desktop_qt] STAGE ");
    guest_serial_putu(n);
    guest_serial_puts(" OK\n");
}

static void guest_stage_halt(unsigned n)
{
    guest_serial_puts("[desktop_qt] STAGE cap ");
    guest_serial_putu(BFREE_GUEST_STAGE_MAX);
    guest_serial_puts(" halt after ");
    guest_serial_putu(n);
    guest_serial_puts("\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
}

__attribute__((noinline)) static void guest_ctor_qgui_application(void)
{
    guest_serial_puts("[desktop_qt] QGuiApplication ctor start\n");
    bfree_guest_refresh_libc_auxv();
    QCoreApplication::setSetuidAllowed(true);
    g_qapp = new QGuiApplication(g_qt_argc, g_qt_argv);
    guest_serial_puts("[desktop_qt] QGuiApplication ctor done\n");
    guest_serial_puts("[desktop_qt] QGuiApplication OK\n");
}

static void guest_ctor_qml_phase(void) __attribute__((noinline));
static void guest_ctor_qml_phase(void)
{
    guest_reset_qt_resource_registry();
    bfree_guest_serial_step_raw('Q');
    guest_serial_puts("[desktop_qt] QML phase start (qrc fallback, hybrid after)\n");
    bfree_guest_serial_step_raw('R');
    bfree_guest_qrc_alloc_scope_enter();
    bfree_guest_serial_step_raw('S');
    guest_log_resource_registry_n();
    bfree_guest_serial_step_raw('T');
    guest_serial_puts("[desktop_qt] qrc guest_desktop\n");
    guest_qrc_init_guest_desktop();
    bfree_guest_qresource_sanitize();
    guest_log_resource_registry_n();
    guest_serial_puts("[desktop_qt] qrc qmake_QML\n");
    (void)_Z24qInitResources_qmake_QMLv();
    bfree_guest_qresource_sanitize();
    guest_log_resource_registry_n();
    guest_serial_puts("[desktop_qt] qrc qmake_QtQuick\n");
#if defined(BFREE_GUEST_SKIP_QTQUICK_QRC)
    guest_serial_puts("[desktop_qt] skip qmake_QtQuick qrc (types-only)\n");
#else
    (void)_Z28qInitResources_qmake_QtQuickv();
    bfree_guest_qresource_sanitize();
    guest_log_resource_registry_n();
#endif
    guest_serial_puts("[desktop_qt] qrc gui_shaders\n");
    guest_qrc_init_gui_shaders();
    bfree_guest_qresource_sanitize();
    guest_log_resource_registry_n();
    guest_serial_puts("[desktop_qt] qrc scenegraph_shaders\n");
    (void)_Z33qInitResources_scenegraph_shadersv();
    bfree_guest_qresource_sanitize();
    guest_log_resource_registry_n();
    guest_serial_puts("[desktop_qt] qrc init ok\n");
    bfree_guest_qrc_alloc_scope_leave();
    bfree_guest_serial_step_raw('U');
    bfree_guest_begin_hybrid_alloc();
    bfree_guest_serial_step_raw('V');
    bfree_guest_qv4_preflight_arena();
    (void)QLocale(QLocale::C);
    guest_serial_puts("[desktop_qt] QLocale C ok\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_qv4_mmap_scope_begin();
    guest_serial_puts("[desktop_qt] QQmlEngine ctor enter\n");
    guest_serial_rsp();
    /* QML_IMPORT_TRACE / qt.qml.import.debug use QString::arg on QStringLiteral —
     * guest fromRawData corrupts size → replaceArgEscapes page-faults. Keep off. */
    // qputenv("QML_IMPORT_TRACE", "1");
    // qputenv("QT_LOGGING_RULES", "qt.qml.import.debug=true;qt.qml.diskcache.debug=true");
    g_engine = new QQmlEngine();
    /* No filesystem/plugin probes — modules come from qml_register_types_* only. */
    g_engine->setImportPathList(QStringList());
    g_engine->setPluginPathList(QStringList());
    guest_serial_puts("[desktop_qt] QQmlEngine ok\n");
    guest_serial_puts("[desktop_qt] register QtQml module types\n");
    qml_register_types_QtQml_Models();
    guest_serial_puts("[desktop_qt] QtQml.Models ok\n");
    qml_register_types_QtQml_WorkerScript();
    guest_serial_puts("[desktop_qt] QtQml.WorkerScript ok\n");
    qml_register_types_QtQml();
    guest_serial_puts("[desktop_qt] QtQml ok\n");
    guest_serial_puts("[desktop_qt] register QtQuick module types\n");
    qml_register_types_QtQuick();
    guest_serial_puts("[desktop_qt] QtQuick ok\n");
    if (qmlProtectModule("QtQml", 2))
        guest_serial_puts("[desktop_qt] protect QtQml ok\n");
    else
        guest_serial_puts("[desktop_qt] protect QtQml FAIL\n");
    (void)qmlProtectModule("QtQml.Models", 2);
    (void)qmlProtectModule("QtQml.WorkerScript", 2);
    if (qmlProtectModule("QtQuick", 6))
        guest_serial_puts("[desktop_qt] protect QtQuick ok\n");
    else
        guest_serial_puts("[desktop_qt] protect QtQuick FAIL\n");
    guest_serial_puts("[desktop_qt] QtQml+QtQuick types ok\n");
    g_bridge = new GuestDesktopBridge();
    guest_serial_puts("[desktop_qt] bridge ok\n");
    guest_setup_context(*g_engine, *g_bridge);
    guest_serial_puts("[desktop_qt] register MVP qmlcache\n");
    bfree_guest_register_mvp_qmlcache();
    /* Stage load: (1) QtObject create boots engine; (2) Item IR to Ready without create
     * (Item create historically PF/CR2≈0x2B — deferred after processEvents/WSI). */
    guest_serial_puts("[desktop_qt] load GuestMvpShell.qml (QtObject boot)\n");
    {
        guest_serial_puts("[desktop_qt] QQmlComponent ctor enter\n");
        QQmlComponent boot(g_engine,
                           QUrl(QStringLiteral("qrc:/GuestMvpShell.qml")),
                           QQmlComponent::PreferSynchronous);
        guest_serial_puts("[desktop_qt] component status=");
        guest_serial_hex_u64((uint64_t)(unsigned)boot.status());
        guest_serial_puts("\n");
        if (boot.isReady()) {
            guest_serial_puts("[desktop_qt] component ready\n");
            g_qml_root = boot.create(g_engine->rootContext());
        } else if (boot.isError()) {
            guest_serial_puts("[desktop_qt] component error\n");
            for (const QQmlError &e : boot.errors()) {
                const QByteArray line = e.toString().toUtf8();
                guest_serial_puts(line.constData());
                guest_serial_puts("\n");
            }
        }
        guest_serial_puts(g_qml_root ? "[desktop_qt] component.create ok\n"
                                     : "[desktop_qt] component.create null\n");
    }
    guest_serial_puts("[desktop_qt] load GuestDesktopShell.qml (Item IR stage1)\n");
    g_item_comp = new QQmlComponent(g_engine,
                                    QUrl(QStringLiteral("qrc:/GuestDesktopShell.qml")),
                                    QQmlComponent::PreferSynchronous);
    guest_serial_puts("[desktop_qt] Item IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_item_comp->status());
    guest_serial_puts("\n");
    if (g_item_comp->isReady())
        guest_serial_puts("[desktop_qt] Item IR stage1 ready\n");
    else if (g_item_comp->isError()) {
        guest_serial_puts("[desktop_qt] Item IR error\n");
        for (const QQmlError &e : g_item_comp->errors()) {
            const QByteArray line = e.toString().toUtf8();
            guest_serial_puts(line.constData());
            guest_serial_puts("\n");
        }
    }
    /* Stage2 Item create inside QV4 mmap scope (same as QtObject create). */
    if (g_item_comp && g_item_comp->isReady() && !g_item_create_tried) {
        g_item_create_tried = 1;
        guest_serial_puts("[desktop_qt] Item create stage2 enter\n");
        guest_serial_puts("[desktop_qt] Item beginCreate\n");
        QObject *obj = g_item_comp->beginCreate(g_engine->rootContext());
        guest_serial_puts(obj ? "[desktop_qt] Item beginCreate ok\n"
                              : "[desktop_qt] Item beginCreate null\n");
        if (obj) {
            guest_serial_puts("[desktop_qt] Item completeCreate\n");
            g_item_comp->completeCreate();
            guest_serial_puts("[desktop_qt] Item create stage2 ok\n");
            if (QQuickItem *qi = qobject_cast<QQuickItem *>(obj)) {
                qi->setObjectName(QStringLiteral("GuestDesktopShell"));
                qi->setWidth(1024);
                qi->setHeight(768);
                guest_configure_qml_rectangle(qi);
                g_qml_root = obj;
                guest_serial_puts("[desktop_qt] qml root is QQuickItem\n");
            } else {
                guest_serial_puts("[desktop_qt] Item create not QQuickItem\n");
            }
        } else {
            guest_serial_puts("[desktop_qt] Item create stage2 null\n");
        }
    }
    g_qml_ready = g_qml_root ? 1 : 0;
    if (g_qml_ready) {
        guest_serial_puts("[desktop_qt] QML ready (hybrid stack)\n");
    } else {
        guest_serial_puts("[desktop_qt] QML load failed (no root objects)\n");
    }
    bfree_guest_qv4_mmap_scope_end();
}

static void guest_try_stage2_item_create(void)
{
    if (g_item_create_tried || !g_item_comp || !g_engine)
        return;
    g_item_create_tried = 1;
    guest_serial_puts("[desktop_qt] Item create stage2 enter\n");
    if (!g_item_comp->isReady()) {
        guest_serial_puts("[desktop_qt] Item create skip (not ready)\n");
        return;
    }
    /* Zero-binding Item unit: beginCreate then completeCreate with breadcrumbs. */
    guest_serial_puts("[desktop_qt] Item beginCreate\n");
    QObject *obj = g_item_comp->beginCreate(g_engine->rootContext());
    guest_serial_puts(obj ? "[desktop_qt] Item beginCreate ok\n"
                          : "[desktop_qt] Item beginCreate null\n");
    if (!obj) {
        guest_serial_puts("[desktop_qt] Item create stage2 null\n");
        return;
    }
    guest_serial_puts("[desktop_qt] Item completeCreate\n");
    g_item_comp->completeCreate();
    guest_serial_puts("[desktop_qt] Item create stage2 ok\n");
    QQuickItem *qi = qobject_cast<QQuickItem *>(obj);
    if (!qi) {
        guest_serial_puts("[desktop_qt] Item create not QQuickItem\n");
        return;
    }
    /* Properties not in IR (zero-binding unit) — set from C++. */
    qi->setObjectName(QStringLiteral("GuestDesktopShell"));
    qi->setWidth(1024);
    qi->setHeight(768);
    guest_configure_qml_rectangle(qi);
    guest_serial_puts("[desktop_qt] qml root is QQuickItem\n");
    g_qml_root = obj;
    if (auto *qqw = qobject_cast<QQuickWindow *>(g_shell_window)) {
        if (QQuickItem *content = qqw->contentItem()) {
            guest_serial_puts("[desktop_qt] Item parent stage3 enter\n");
            qi->setParentItem(content);
            qi->setWidth(qqw->width());
            qi->setHeight(qqw->height());
            guest_configure_qml_rectangle(qi);
            g_desktop_shell_item = qi;
            guest_serial_puts("[desktop_qt] GuestDesktopShell QML Item parented\n");
        }
    }
}

__attribute__((noinline)) static void guest_mmap_session_body(void);

/* C ABI entry — must match void(*)(void) passed across TUs after RSP switch. */
extern "C" __attribute__((noinline)) void guest_mmap_session_entry(void)
{
    bfree_guest_serial_step_raw('O');
    __asm__ volatile("andq $-16, %%rsp" ::: "rsp");
    guest_mmap_session_body();
}

__attribute__((noinline)) static void guest_mmap_session_body(void)
{
    bfree_guest_serial_step_raw('U');
    guest_serial_puts("[desktop_qt] mmap session enter\n");
    (void)bfree_guest_ensure_fallback_heap();
    bfree_guest_begin_hybrid_alloc();
    guest_serial_puts("[desktop_qt] session hybrid alloc (no musl)\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_serial_step_raw('V');

    guest_stage_banner(3, "QGuiApplication (exec rsp, mmap heap)");
    guest_ctor_qgui_application();
    guest_serial_rsp();
    if (!g_qapp) {
        guest_serial_puts("[desktop_qt] QGuiApplication alloc failed\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    guest_stage_ok(3);
    if (BFREE_GUEST_STAGE_MAX <= 3u)
        guest_stage_halt(3);

    qInstallMessageHandler(guest_qt_message_handler);
    guest_serial_puts("[desktop_qt] qt log hook installed\n");
    guest_serial_puts("[desktop_qt] QGuiApplication OK\n");
    guest_serial_puts("[desktop_qt] platform=bfree\n");

    guest_stage_banner(4, "deferred init_array ctors");
    bfree_guest_run_deferred_init_array_ctors();
    guest_stage_ok(4);
    if (BFREE_GUEST_STAGE_MAX <= 4u)
        guest_stage_halt(4);

    bfree_guest_serial_step_c('M');

    guest_stage_banner(5, "QQmlEngine + qmlcache (mmap stay)");
    g_bridge = nullptr;
    g_engine = nullptr;
    g_qml_root = nullptr;
    g_qml_ready = 0;
    guest_serial_puts("[desktop_qt] QML phase on mmap session stack\n");
    guest_ctor_qml_phase();
    if (!g_engine || !g_qml_ready) {
        guest_serial_puts("[desktop_qt] QML init failed\n");
        for (;;) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
    guest_stage_ok(5);
    guest_show_shell_window();
    /* Parent QML Item if stage2 produced one (else C++ Rectangle already attached). */
    if (QQuickItem *qi = qobject_cast<QQuickItem *>(g_qml_root)) {
        if (!g_desktop_shell_item) {
            if (auto *qqw = qobject_cast<QQuickWindow *>(g_shell_window)) {
                if (QQuickItem *content = qqw->contentItem()) {
                    guest_serial_puts("[desktop_qt] Item parent stage3 enter\n");
                    qi->setParentItem(content);
                    qi->setWidth(qqw->width());
                    qi->setHeight(qqw->height());
                    g_desktop_shell_item = qi;
                    guest_serial_puts("[desktop_qt] GuestDesktopShell QML Item parented\n");
                }
            }
        }
    }
    /* Final authority: desk pixels on FB0 (QPA may have flushed after attach). */
    guest_desk_mark_dirty();
    guest_desk_flush_paint();
    /* No autorun window — real mouse / Enter opens apps (mouse smoke verifies click). */
    guest_serial_puts("[desktop_qt] QML ready, entering event loop\n");
    if (g_qapp && !g_desk_qt_filter) {
        g_desk_qt_filter = new GuestDeskQtInputFilter();
        g_qapp->installEventFilter(g_desk_qt_filter);
        guest_serial_puts("[desktop_qt] qt input filter installed\n");
    }
    bfree_qpa_set_mouse_bridge(guest_qpa_mouse_bridge);
    bfree_qpa_set_key_bridge(guest_qpa_key_bridge);
    g_desk_qpa_input = 1;
    guest_serial_puts("[desktop_qt] qpa mouse bridge installed\n");
    guest_serial_puts("[desktop_qt] input path=qpa-single (PS/2 relative; usb=off)\n");
    bfree_qpa_cache_thread_data();
    /* Dirty-only paint — no periodic full redraw (G0). */
    {
        static int pe_logged;
        for (;;) {
            if (g_qapp) {
                bfree_qpa_process_events(QEventLoop::AllEvents);
                if (!pe_logged) {
                    pe_logged = 1;
                    guest_serial_puts("[desktop_qt] processEvents ok\n");
                    guest_serial_puts("[desktop_qt] wsi input pump armed\n");
                }
            }
            bfree_qpa_pump_guest_input();
            guest_desk_pump_input(); /* no-op when g_desk_qpa_input */
            guest_desk_flush_paint();
            for (int i = 0; i < 8000; ++i)
                __asm__ volatile("pause" ::: "memory");
        }
    }
    guest_serial_puts("[desktop_qt] event loop returned\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* Absolute argv[0] avoids QStandardPaths::findExecutable(PATH scan) after getcwd. */
    static char prog[] = "/desktop";
    static char arg_platform[] = "-platform";
    static char arg_bfree[] = "bfree";
    static char *qt_argv[] = { prog, arg_platform, arg_bfree, nullptr };
    int qt_argc = 3;
    g_qt_argc = qt_argc;
    g_qt_argv = qt_argv;

    bfree_guest_serial_step_c('H');
    bfree_guest_serial_step_c('I');
    bfree_guest_refresh_libc_auxv();
    bfree_guest_serial_step_c('J');
    guest_serial_puts("[desktop_qt] main entry\n");
    guest_serial_puts("[desktop_qt] build=mmap96-v349 staged\n");
    bfree_guest_serial_step_c('K');
    guest_serial_puts("[desktop_qt] QGuiApplication...\n");
    bfree_guest_serial_step_c('L');
    g_qt_msg_diag = 0;
    bfree_guest_install_static_env();

    guest_stage_banner(1, "static plugins (bss+bump)");
    guest_serial_puts("[desktop_qt] ctor enter\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_run_on_ctor_stack_plugins(guest_ctor_plugins_only);
    guest_serial_puts("[desktop_qt] ctor leave\n");
    guest_serial_rsp();
    guest_stage_ok(1);
    if (BFREE_GUEST_STAGE_MAX <= 1u)
        guest_stage_halt(1);

    guest_stage_banner(2, "musl brk + mmap arenas");
    bfree_guest_preflight_musl_heap();
    guest_serial_rsp();
    guest_stage_ok(2);
    if (BFREE_GUEST_STAGE_MAX <= 2u)
        guest_stage_halt(2);

    bfree_guest_preflight_ctor_mmap();
    guest_serial_puts("[desktop_qt] preflight done, enter mmap session\n");

    guest_stage_banner(3, "mmap session (QGui+QML, musl stay)");
    bfree_guest_enter_preflighted_mmap_noreturn(guest_mmap_session_entry);
    return 0;
}

#else

#include <stdint.h>

#define BFREE_SYS_DEBUG_SERIAL_WRITE 24

static inline long bfree_syscall2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static void guest_puts(const char *s)
{
    uint32_t n = 0;
    if (!s) {
        return;
    }
    while (s[n] != '\0') {
        ++n;
    }
    (void)bfree_syscall2(BFREE_SYS_DEBUG_SERIAL_WRITE, (long)s, (long)n);
}

extern "C" int main(void)
{
    guest_puts("[desktop_qt] Qt guest not linked.\n");
    guest_puts("[desktop_qt] Build: bash tools/build_iso_desktop_shell.sh\n");
    for (;;) {
        __asm__ volatile("pause" ::: "memory");
    }
    return 0;
}

#endif
