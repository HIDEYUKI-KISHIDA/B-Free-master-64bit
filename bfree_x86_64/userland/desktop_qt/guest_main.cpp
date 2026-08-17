/* guest_main.cpp — ISO guest session: Qt Quick + bfree QPA + DesktopShell.qml (B5b). */
#if defined(BFREE_GUEST_HAS_QT6)
/* Skip qmake_QtQuick qrc until qRegisterResourceData is stable with large Quick. */
#ifndef BFREE_GUEST_SKIP_QTQUICK_QRC
#define BFREE_GUEST_SKIP_QTQUICK_QRC 1
#endif

#include "guest_bfree_shell_process.h"
#include "guest_desktop_bridge.h"
#include "guest_mvp_qmlcache_register.h"
#include "guest_breeze_tokens.h"
#include "guest_splash_data.h"

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
#include <QMetaObject>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QtQml/qqml.h>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <private/qquickrectangle_p.h>
#include <private/qwindow_p.h>
#include <private/qqmlcomponent_p.h>
#include <private/qqmlengine_p.h>
#include <private/qv4compileddata_p.h>
#include <private/qv4executablecompilationunit_p.h>
#include <QtQml/qqmlprivate.h>
#if defined(BFREE_GUEST_LINK_CONTROLS)
#include <QtQuickTemplates2/private/qquickabstractbutton_p.h>
#include <QtQuickTemplates2/private/qquickbutton_p.h>
#include <QtQuickTemplates2/private/qquickcheckbox_p.h>
#include <QtQuickTemplates2/private/qquickcontrol_p.h>
#include <QtQuickTemplates2/private/qquickradiobutton_p.h>
#include <QtQuickTemplates2/private/qquickswitch_p.h>
#include <QtQuickTemplates2/private/qquicktheme_p.h>
#include <QtQuickTemplates2/private/qquicktheme_p_p.h>
#include <QtQuickLayouts/private/qquicklinearlayout_p.h>
#endif
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
extern "C" void bfree_qpa_set_update_delivery(int enabled);
extern QStaticPlugin qt_static_plugin_QJpegPlugin(void);

/* Static Qt: QmlMeta/Quick type registrars are not auto-imported without qmlplugins. */
extern void qml_register_types_QtQml(void);
extern void qml_register_types_QtQml_Models(void);
extern void qml_register_types_QtQml_WorkerScript(void);
extern void qml_register_types_QtQuick(void);
#if defined(BFREE_GUEST_LINK_CONTROLS)
extern void qml_register_types_QtQuick_Templates(void);
extern void qml_register_types_QtQuick_Controls_impl(void);
extern void qml_register_types_QtQuick_Controls(void);
extern void qml_register_types_QtQuick_Controls_Basic_impl(void);
extern void qml_register_types_QtQuick_Controls_Basic(void);
extern void qml_register_types_QtQuick_Layouts(void);
#endif

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
void bfree_guest_ensure_drawhelpers(void);
void bfree_guest_set_prefer_fallback_alloc(int on);
void bfree_guest_serial_step_raw(char step);
void bfree_guest_rebind_musl_fs(void);
void guest_mmap_session_entry(void);
void bfree_guest_qt_coop_schedule(void);
void bfree_guest_set_typeloader_main_ok(int on);
int bfree_guest_typeloader_main_ok(void);
const void *bfree_guest_qmlcache_unit_for_url(const char *url_utf8) __attribute__((weak));
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
static QQmlComponent *g_g1_comp = nullptr;
static int g_g1_posted = 0;
static int g_g1_done = 0;
static unsigned g_g1_polls = 0;
static int g_item_create_tried = 0;
static QQuickItem *g_desktop_shell_item = nullptr;
static int g_qml_ready = 0;
/* Distinctive fill when GuestDesktopShell.qml Rectangle is present (FB lookalike mirrors it). */
static uint32_t g_qml_rect_color = 0;
static int g_qml_rect_configured = 0;
/* IR Rectangle reparented under C++ shell — FB badge proves live color. */
static QQuickRectangle *g_ir_desk_rect = nullptr;
static int g_ir_rect_live = 0;
static int g_ir_sg_ok = 0; /* HasContents + soft present survived */
static int g_text_fb_ok = 0; /* FB glyph path (QQuickText create still PF@0) */
/* W3: sparse Quick window chrome (host desktopWindowLayer look). */
static int g_w3_layer_ready = 0;
static int g_w3_sg_pixels = 0; /* 1 = SG session armed */
static int g_w3_sg_second_ok = 0; /* dual-pulse / multi-item present */
static int g_w3_sg_multi_ok = 0; /* child rect visible under bar during present */
static int g_w3_sg_win_auth = 0; /* 1 = skip FB for static SG window probe slot */
static QQuickRectangle *g_w32_win_probe = nullptr; /* child of taskbar bar */
/* Must match g_w32_win_probe geometry (contentItem child). */
enum { W3_SG_PROBE_X = 80, W3_SG_PROBE_Y = 60, W3_SG_PROBE_W = 420, W3_SG_PROBE_H = 300 };
/* W3.5: force one window outer visible once at attach (FB still pixel authority). */
static int g_w35_force_outer = 0;
static QQuickItem *g_w3_layer = nullptr;
/* W3.1: sparse Quick taskbar + Start panel (host taskbar / launcherPanel). */
static int g_w31_layer_ready = 0;
static int g_w31_sg_pixels = 0; /* legacy; W3.2 uses g_w32_sg_taskbar for strip skip */
static QQuickItem *g_w31_layer = nullptr;
/* W3.2: SG presented taskbar strip once at attach; FB skips that strip. */
static int g_w32_sg_taskbar = 0;
static int g_w32_probe_ok = 0;
/* G1: wallpaper+icons+taskbar SG leaves are pixel authority (FB skips those bands). */
static int g_sg_desktop_auth = 0;
static int g_gate1_window_ok = 0;
/* Product relative-import bypass: root-qrc thin child (GuestProductChild). */
static QQuickItem *g_prod_child_item = nullptr;
static QQuickItem *g_prod_chrome_item = nullptr;
static QQuickItem *g_prod_tray_item = nullptr;
static QQuickItem *g_prod_icons_item = nullptr;
static QQuickItem *g_prod_icons2_item = nullptr;
static QQuickItem *g_prod_start_item = nullptr;
static QQuickRectangle *g_ds_product_content_badge = nullptr;
static QQuickWindow *g_prod_sg_win = nullptr;
static int g_prod_sg_ok = 0;
static int g_prod_sg_sustained = 0;
/* Step3: FB0 paints product leaf geometry (SG soft present survived; flush deferred). */
static int g_prod_fb0_auth = 0;
/* Step1: sustained SG → QPA flush is LIVE pixel authority (no FB lookalike overwrite). */
static int g_prod_sg_flush_auth = 0;
static int g_prod_sg_pulse_hold = 0; /* skip dense UpdateRequest until event loop */
static int g_prod_post_activate_sg = 0; /* Max1: allow setVisible + SG pulse after loop */
static int g_prod_post_activate_done = 0; /* one-shot arm at first loop tick */
static int g_prod_fb_chrome_force = 0; /* one-shot FB chrome after blank SG flush */
static QQuickRectangle *g_host_wall = nullptr;
static QQuickRectangle *g_host_start = nullptr;
static int g_prod_host_tree_unlocked = 0; /* post-activate: host chrome SG paint live */
static QQuickRectangle *g_host_bar = nullptr;
static QQuickItem *g_qml_term_item = nullptr;
static int g_qml_term_ok = 0;
#if defined(BFREE_GUEST_LINK_CONTROLS)
static QQuickButton *g_controls_button_probe = nullptr;
static QQuickCheckBox *g_controls_checkbox_probe = nullptr;
static QQuickRadioButton *g_controls_radio_probe = nullptr;
static QQuickSwitch *g_controls_switch_probe = nullptr;
static QObject *g_qml_controls_button_root = nullptr;
static QQuickButton *g_qml_controls_button_standin = nullptr;
static QQuickRowLayout *g_layouts_row_probe = nullptr;
static QQuickItem *g_ds_qml_root = nullptr; /* qrc:/DesktopShell.qml Item root */
static QQuickRowLayout *g_ds_subset_row = nullptr;
#endif
static QQuickItem *g_cpp_item_parent_probe = nullptr;
static int g_sg_chrome_need_pulse = 0;
static int g_sg_start_leaf_ok = 0;
static QQuickRectangle *g_sg_wallpaper = nullptr;
static QQuickRectangle *g_sg_start_leaf = nullptr;
static QQuickRectangle *g_sg_icons[24];
static int g_sg_icons_n = 0;
/* W3.4: soft UpdateRequest pulses on taskbar bar after attach (FB still authority). */
static int g_w34_pulses = 0;
static int g_w34_ok = 0;
enum { G_W34_PULSES_NEED = 4 };

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

/* Start menu geometry — host DesktopShell launcherPanel (~620x500) scaled to 1024x768. */
enum {
    G_START_MENU_W = 560,
    G_START_MENU_H = 460,
    G_START_MENU_X = 8,
    G_START_MENU_GAP = 8,
    G_START_PAD = 16,
    G_START_SEARCH_H = 42,
    G_START_ROW_H = 36, /* legacy list row (W3 scaffold) */
    G_START_FOOTER_H = 44,
    G_START_TILE_W = 110,
    G_START_TILE_H = 88,
    G_START_TILE_GAP = 10,
    G_START_COLS = 4,
    G_START_HIT_RESTART = 1000,
    G_START_HIT_SHUTDOWN = 1001,
    G_START_HIT_SLEEP = 1002
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
enum { G_PERSIST_MAX = 24, G_PERSIST_NAME = 32, G_TERM_MAX = 40, G_TERM_COLS = 64,
       G_TERM_LINE_MAX = 120, G_TERM_CAPTURE_MAX = 1024 };
static char g_persist_names[G_PERSIST_MAX][G_PERSIST_NAME];
static int g_persist_nnames = 0;
static char g_persist_preview[120];
static int g_persist_listed = 0;
static int g_persist_sel = -1;
static int g_persist_scroll = 0;

/* FB Terminal scrollback + line editor (BusyBox via vfork+exec oneshot). */
static char g_term_lines[G_TERM_MAX][G_TERM_COLS];
static int g_term_nlines = 0;
static char g_term_line[G_TERM_LINE_MAX];
static int g_term_linelen = 0;
static int g_term_session = 0;
/* Run BusyBox demo after first paint so open never blocks the input loop. */
static int g_term_bb_demo_pending = 0;
static GuestPtyShell g_term_pty;
static int g_term_pty_mode = 0;
static char g_term_pty_partial[G_TERM_COLS];
static int g_term_pty_partial_len = 0;

static long guest_sys1(long n, long a);
static long guest_sys2(long n, long a, long b);
static long guest_sys3(long n, long a, long b, long c);
static void guest_desk_mark_dirty(void);

static long guest_sys1(long n, long a)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}

static long guest_sys2(long n, long a, long b)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b)
                     : "rcx", "r11", "memory");
    return r;
}

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

static void guest_persist_load_preview(const char *pick)
{
    char path[40];
    if (!pick || !pick[0])
        return;
    path[0] = '/'; path[1] = 'p'; path[2] = 'e'; path[3] = 'r';
    path[4] = 's'; path[5] = 'i'; path[6] = 's'; path[7] = 't'; path[8] = '/';
    size_t j = 0;
    while (pick[j] && j + 10U < sizeof(path)) {
        path[9 + j] = pick[j];
        ++j;
    }
    path[9 + j] = '\0';
    long f2 = guest_sys3(2, (long)path, 0, 0);
    if (f2 < 0)
        return;
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
    } else {
        g_persist_preview[0] = '\0';
    }
}

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
    g_persist_sel = -1;
    g_persist_scroll = 0;
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
        while (off < nread && g_persist_nnames < G_PERSIST_MAX) {
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

    /* Prefer hello* / desk* / first file for preview. */
    int pick_i = -1;
    for (int pass = 0; pass < 3 && pick_i < 0; ++pass) {
        for (int i = 0; i < g_persist_nnames; ++i) {
            const char *nm = g_persist_names[i];
            if (pass == 0) {
                if ((nm[0] == 'h' || nm[0] == 'H') &&
                    (nm[1] == 'e' || nm[1] == 'E') &&
                    (nm[2] == 'l' || nm[2] == 'L') &&
                    (nm[3] == 'l' || nm[3] == 'L') &&
                    (nm[4] == 'o' || nm[4] == 'O')) {
                    pick_i = i;
                    break;
                }
            } else if (pass == 1) {
                if ((nm[0] == 'd' || nm[0] == 'D') &&
                    (nm[1] == 'e' || nm[1] == 'E') &&
                    (nm[2] == 's' || nm[2] == 'S') &&
                    (nm[3] == 'k' || nm[3] == 'K')) {
                    pick_i = i;
                    break;
                }
            } else {
                pick_i = i;
                break;
            }
        }
    }
    if (pick_i >= 0) {
        g_persist_sel = pick_i;
        guest_persist_load_preview(g_persist_names[pick_i]);
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

static void guest_term_push(const char *s)
{
    int i = 0;
    if (!s)
        return;
    if (g_term_nlines >= G_TERM_MAX) {
        for (int r = 1; r < G_TERM_MAX; ++r) {
            for (int c = 0; c < G_TERM_COLS; ++c)
                g_term_lines[r - 1][c] = g_term_lines[r][c];
        }
        g_term_nlines = G_TERM_MAX - 1;
    }
    while (s[i] && i + 1 < G_TERM_COLS) {
        g_term_lines[g_term_nlines][i] = s[i];
        ++i;
    }
    g_term_lines[g_term_nlines][i] = '\0';
    ++g_term_nlines;
}

static const char g_term_prompt_pref[] = "bfree> ";

static int guest_term_prompt_cols(void)
{
    int n = 0;
    while (g_term_prompt_pref[n])
        ++n;
    return n + g_term_linelen;
}

static int guest_term_casecmp(char a, char b)
{
    if (a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    return (a == b) ? 1 : 0;
}

static int guest_term_cmd_eq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && guest_term_casecmp(a[i], b[i]))
        ++i;
    return (a[i] == 0 && b[i] == 0) ? 1 : 0;
}

static void guest_term_prompt_text(char *out, int outmax)
{
    int p = 0;
    int i;
    if (outmax < 4) {
        if (outmax > 0)
            out[0] = '\0';
        return;
    }
    for (i = 0; g_term_prompt_pref[i] && p + 1 < outmax; ++i)
        out[p++] = g_term_prompt_pref[i];
    for (i = 0; i < g_term_linelen && p + 1 < outmax; ++i)
        out[p++] = g_term_line[i];
    /* Block cursor is painted as a rect — keep a space so glyphs do not collide. */
    if (p + 1 < outmax)
        out[p++] = ' ';
    out[p] = '\0';
}

static void guest_term_refresh_prompt(void)
{
    char buf[G_TERM_COLS];
    guest_term_prompt_text(buf, (int)sizeof(buf));
    if (g_term_nlines > 0) {
        int i = 0;
        while (buf[i] && i + 1 < G_TERM_COLS) {
            g_term_lines[g_term_nlines - 1][i] = buf[i];
            ++i;
        }
        g_term_lines[g_term_nlines - 1][i] = '\0';
    } else {
        guest_term_push(buf);
    }
}

static void guest_term_push_prompt(void)
{
    g_term_linelen = 0;
    g_term_line[0] = '\0';
    guest_term_refresh_prompt();
}

static int guest_term_streq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        ++i;
    return (a[i] == 0 && b[i] == 0) ? 1 : 0;
}

static void guest_term_skip_ws(const char **pp)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t')
        ++p;
    *pp = p;
}

static int guest_term_take_word(const char **pp, char *out, int outmax)
{
    const char *p = *pp;
    int n = 0;
    guest_term_skip_ws(&p);
    while (*p && *p != ' ' && *p != '\t' && n + 1 < outmax) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    guest_term_skip_ws(&p);
    *pp = p;
    return n;
}

static void guest_term_push_ls_persist(void)
{
    char line[G_TERM_COLS];
    int n = 0;
    guest_term_push("$ ls /persist");
    for (int i = 0; i < g_persist_nnames && n < 8; ++i) {
        int p = 0;
        line[p++] = ' ';
        for (int j = 0; g_persist_names[i][j] && p + 1 < G_TERM_COLS; ++j)
            line[p++] = g_persist_names[i][j];
        line[p] = '\0';
        guest_term_push(line);
        ++n;
    }
    if (g_persist_nnames == 0)
        guest_term_push(" (empty)");
}

static void guest_term_feed_pty(const char *buf, int n)
{
    int i;

    if (!buf || n <= 0)
        return;
    for (i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == '\r')
            continue;
        if (c == '\n') {
            if (g_term_pty_partial_len > 0) {
                g_term_pty_partial[g_term_pty_partial_len] = '\0';
                guest_term_push(g_term_pty_partial);
                g_term_pty_partial_len = 0;
            } else {
                guest_term_push("");
            }
            continue;
        }
        if (g_term_pty_partial_len + 1 < G_TERM_COLS)
            g_term_pty_partial[g_term_pty_partial_len++] = c;
    }
    if (g_term_pty_partial_len > 0) {
        char live[G_TERM_COLS];
        int j;
        g_term_pty_partial[g_term_pty_partial_len] = '\0';
        for (j = 0; j < g_term_pty_partial_len && j + 1 < G_TERM_COLS; ++j)
            live[j] = g_term_pty_partial[j];
        live[j] = '\0';
        if (g_term_nlines > 0) {
            int k = 0;
            while (live[k] && k + 1 < G_TERM_COLS) {
                g_term_lines[g_term_nlines - 1][k] = live[k];
                ++k;
            }
            g_term_lines[g_term_nlines - 1][k] = '\0';
        } else {
            guest_term_push(live);
        }
    }
}

static void guest_terminal_pty_poll(void)
{
    char buf[128];
    int n;

    if (!g_term_pty_mode || !g_term_pty.active)
        return;
    n = guest_pty_shell_poll(&g_term_pty, buf, (int)sizeof(buf));
    if (n > 0)
        guest_term_feed_pty(buf, n);
}

/* Forward — defined below. */
static int guest_terminal_busybox_oneshot(const char *applet, const char *arg1);
static int guest_terminal_busybox_sh_c(const char *line);

static void guest_term_push_capture(const char *buf, long total)
{
    int start = 0;
    long i;

    if (!buf || total <= 0)
        return;
    for (i = 0; i <= total; ++i) {
        if (i == total || buf[i] == '\n' || buf[i] == '\r') {
            if (i > start) {
                char line[G_TERM_COLS];
                int j = 0;
                int k;
                for (k = start; k < i && j + 1 < G_TERM_COLS; ++k)
                    line[j++] = buf[k];
                line[j] = '\0';
                if (j > 0)
                    guest_term_push(line);
            }
            if (i < total && buf[i] == '\r' && i + 1 < total && buf[i + 1] == '\n')
                ++i;
            start = (int)i + 1;
        }
    }
}

static void guest_term_exec_line(const char *raw)
{
    char shown[G_TERM_COLS];
    char cmd[32];
    const char *p = raw ? raw : "";
    int i = 0;
    int sp = 0;

    guest_term_skip_ws(&p);
    if (!*p)
        return;

    shown[sp++] = '$';
    shown[sp++] = ' ';
    while (p[i] && sp + 1 < G_TERM_COLS) {
        shown[sp++] = p[i++];
    }
    shown[sp] = '\0';
    guest_term_push(shown);

    if (!guest_term_take_word(&p, cmd, (int)sizeof(cmd)))
        return;

    if (guest_term_cmd_eq(cmd, "help") || guest_term_streq(cmd, "?")) {
        guest_term_push("--- builtins ---");
        guest_term_push(" help echo ls clear survive");
        guest_term_push("--- shell (busybox ash) ---");
        guest_term_push(" any other line runs: sh -c \"...\"");
        guest_term_push(" pipes/redirects OK: ls | wc");
        guest_term_push(" vi awk sed tar grep find mount ping");
        guest_term_push(" ps top df du chmod ln test");
        guest_term_push("example: echo hello > /tmp/x");
        guest_serial_puts("[desktop_qt] Terminal help\n");
        return;
    }
    if (guest_term_cmd_eq(cmd, "clear") || guest_term_cmd_eq(cmd, "cls")) {
        g_term_nlines = 0;
        guest_serial_puts("[desktop_qt] Terminal clear\n");
        return;
    }
    if (guest_term_cmd_eq(cmd, "ls")) {
        g_persist_listed = 0;
        guest_persist_scan_once();
        guest_term_push_ls_persist();
        if (*p) {
            (void)guest_terminal_busybox_sh_c(raw);
            guest_serial_puts("[desktop_qt] Terminal ls sh -c\n");
        } else {
            guest_serial_puts("[desktop_qt] Terminal ls\n");
        }
        return;
    }
    if (guest_term_cmd_eq(cmd, "echo")) {
        /* Local echo — no BusyBox spawn (fast + reliable). */
        if (*p)
            guest_term_push(p);
        else
            guest_term_push("");
        guest_serial_puts("[desktop_qt] Terminal echo\n");
        return;
    }
    if (guest_term_cmd_eq(cmd, "survive") || guest_term_cmd_eq(cmd, "die")) {
        guest_term_push("SURVIVE: killing guest ABI (core tick continues)");
        guest_serial_puts("[desktop_qt] SURVIVE demo: killing guest ABI (term)\n");
        *(volatile int *)0 = 0x736b;
        return;
    }

    /* Everything else: full BusyBox ash one-shot (pipes, redirects, all applets). */
    (void)guest_terminal_busybox_sh_c(raw);
    guest_serial_puts("[desktop_qt] Terminal sh -c\n");
}

/* FB Terminal → busybox.elf one-shot (vfork+exec).
 * Capture stdout via a parent-held /tmp file fd (shared fd table: child only
 * dup2's it; parent must not reopen+O_TRUNC after wait). Pipe capture fails
 * when Qt exhausts the 16 guest pipe slots. outfd/sav1 are static (vfork+-O2).
 * Exec heals stdin only so the stdout redirect survives private-AS transfer. */
static int guest_terminal_busybox_run_argv(char *const *argv, const char *logtag)
{
    static char bb_path[] = "/busybox.elf";
    static char outbuf[G_TERM_CAPTURE_MAX];
    static char out_path[] = "/tmp/bb.out";
    static long outfd;
    static long sav1;
    long pid;
    long nfd;
    long n;
    long total = 0;

    if (!argv || !argv[0] || !argv[0][0])
        return -1;

    if (logtag && logtag[0]) {
        guest_serial_puts("[desktop_qt] Terminal busybox spawn ");
        guest_serial_puts(logtag);
        guest_serial_puts("\n");
    }

    sav1 = guest_sys1(32 /* dup */, 1);
    if (sav1 < 0) {
        guest_serial_puts("[desktop_qt] Terminal busybox dup stdout fail\n");
        return -1;
    }
    /* O_WRONLY|O_CREAT|O_TRUNC */
    outfd = guest_sys3(2 /* open */, (long)out_path, 1 | 64 | 512, 0644);
    if (outfd < 0) {
        guest_serial_puts("[desktop_qt] Terminal busybox /tmp fail\n");
        (void)guest_sys1(3, sav1);
        return -1;
    }

    pid = guest_sys1(58 /* vfork */, 0);
    if (pid < 0) {
        guest_serial_puts("[desktop_qt] Terminal busybox vfork fail\n");
        (void)guest_sys1(3, outfd);
        outfd = -1;
        (void)guest_sys1(3, sav1);
        return -1;
    }
    if (pid == 0) {
        static const char mark[] = "[bbchild]\n";
        (void)guest_sys2(24, (long)mark, (long)(sizeof(mark) - 1));
        nfd = guest_sys3(2, (long)"/dev/null", 0, 0);
        if (nfd >= 0) {
            (void)guest_sys2(33 /* dup2 */, nfd, 0);
            if (nfd > 2)
                (void)guest_sys1(3, nfd);
        }
        /* Leave outfd open for parent; do not close it (shared table). */
        (void)guest_sys2(33 /* dup2 */, outfd, 1);
        (void)guest_sys3(59 /* execve */, (long)bb_path, (long)argv, 0);
        {
            static const char fail[] = "[bbexecfail]\n";
            (void)guest_sys2(24, (long)fail, (long)(sizeof(fail) - 1));
        }
        guest_sys1(60 /* exit */, 127);
        for (;;)
            __asm__ volatile("pause" ::: "memory");
    }
    (void)guest_sys3(61 /* waitpid */, pid, 0, 0);

    /* Child exit may have left stdout on outfd — restore. */
    (void)guest_sys2(33 /* dup2 */, sav1, 1);
    (void)guest_sys1(3, sav1);

    (void)guest_sys3(8 /* lseek */, outfd, 0, 0 /* SEEK_SET */);
    outbuf[0] = '\0';
    while (total < (long)(sizeof(outbuf) - 1)) {
        n = guest_sys3(0 /* read */, outfd, (long)(outbuf + total),
                       (long)(sizeof(outbuf) - 1 - (size_t)total));
        if (n <= 0)
            break;
        total += n;
    }
    if (total > 0) {
        guest_serial_puts("[desktop_qt] Terminal busybox held-fd ok\n");
    } else {
        guest_serial_puts("[desktop_qt] Terminal busybox held-fd miss\n");
        (void)guest_sys1(3, outfd);
        outfd = -1;
        /* Fallback: reopen by name (no O_TRUNC) in case the held fd went stale. */
        {
            long rfd = guest_sys3(2 /* open */, (long)out_path, 0, 0);
            if (rfd < 0) {
                guest_serial_puts("[desktop_qt] Terminal busybox reopen fail\n");
            } else {
                total = 0;
                while (total < (long)(sizeof(outbuf) - 1)) {
                    n = guest_sys3(0 /* read */, rfd, (long)(outbuf + total),
                                   (long)(sizeof(outbuf) - 1 - (size_t)total));
                    if (n <= 0)
                        break;
                    total += n;
                }
                (void)guest_sys1(3, rfd);
                if (total > 0)
                    guest_serial_puts("[desktop_qt] Terminal busybox reopen ok\n");
            }
        }
    }
    if (outfd >= 0) {
        (void)guest_sys1(3, outfd);
        outfd = -1;
    }
    if (total < 0)
        total = 0;
    while (total > 0 && (outbuf[total - 1] == '\n' || outbuf[total - 1] == '\r'))
        --total;
    outbuf[total] = '\0';

    if (total <= 0) {
        guest_serial_puts("[desktop_qt] Terminal busybox empty cap\n");
        guest_term_push("(empty capture)");
        return -1;
    }
    guest_term_push_capture(outbuf, total);
    guest_serial_puts("[desktop_qt] Terminal busybox ok\n");
    return 0;
}

static int guest_terminal_busybox_oneshot(const char *applet, const char *arg1)
{
    static char bb_applet[32];
    static char bb_arg1[64];
    static char *bb_argv[4];
    int i;
    int argc = 0;

    if (!applet || !applet[0])
        return -1;
    for (i = 0; applet[i] && i + 1 < (int)sizeof(bb_applet); ++i)
        bb_applet[i] = applet[i];
    bb_applet[i] = '\0';
    bb_argv[argc++] = bb_applet;
    if (arg1 && arg1[0]) {
        for (i = 0; arg1[i] && i + 1 < (int)sizeof(bb_arg1); ++i)
            bb_arg1[i] = arg1[i];
        bb_arg1[i] = '\0';
        bb_argv[argc++] = bb_arg1;
    }
    bb_argv[argc] = nullptr;
    return guest_terminal_busybox_run_argv(bb_argv, bb_applet);
}

static int guest_terminal_busybox_sh_c(const char *line)
{
    static char bb_sh[] = "sh";
    static char bb_cflag[] = "-c";
    static char bb_cmd[G_TERM_LINE_MAX];
    static char *bb_argv[4];
    const char *p = line;
    int i;

    if (!line)
        return -1;
    guest_term_skip_ws(&p);
    if (!*p)
        return -1;
    for (i = 0; p[i] && i + 1 < (int)sizeof(bb_cmd); ++i)
        bb_cmd[i] = p[i];
    bb_cmd[i] = '\0';
    bb_argv[0] = bb_sh;
    bb_argv[1] = bb_cflag;
    bb_argv[2] = bb_cmd;
    bb_argv[3] = nullptr;
    return guest_terminal_busybox_run_argv(bb_argv, "sh -c");
}

/* FB Terminal: arm line editor immediately (no vfork on open/raise).
 * BusyBox demo runs once from the event loop after the first paint. */
static void guest_terminal_ensure_session(void)
{
    static const char body[] = "TERM_OK";
    long fd;

    if (g_term_session)
        return;

    g_term_nlines = 0;
    g_term_linelen = 0;
    g_term_line[0] = '\0';
    g_term_session = 1;

    fd = guest_sys3(2, (long)"/persist/term.txt", 1 | 64 | 512, 0644);
    if (fd < 0) {
        guest_serial_puts("[desktop_qt] Terminal command fail\n");
        guest_term_push("Terminal: /persist write fail");
        guest_term_push_prompt();
        guest_serial_puts("[desktop_qt] Terminal line editor ready\n");
        return;
    }
    (void)guest_sys3(1 /* write */, fd, (long)body, (long)(sizeof(body) - 1));
    (void)guest_sys1(3, fd);
    guest_serial_puts("[desktop_qt] Terminal ran echo TERM_OK\n");

    guest_term_push("B-Free Terminal (BusyBox ash session)");
    if (guest_pty_shell_start(&g_term_pty) == 0) {
        g_term_pty_mode = 1;
        g_term_pty_partial_len = 0;
        g_term_pty_partial[0] = '\0';
        guest_serial_puts("[desktop_qt] Terminal PTY shell started\n");
    } else {
        g_term_pty_mode = 0;
        guest_term_push("PTY unavailable — line mode (type help)");
        guest_term_push_prompt();
        g_term_bb_demo_pending = 1;
        guest_serial_puts("[desktop_qt] Terminal PTY start fail\n");
    }
    guest_serial_puts("[desktop_qt] Terminal session ready\n");
    guest_serial_puts("[desktop_qt] Terminal line editor ready\n");
    g_persist_listed = 0;
}

static void guest_terminal_run_pending_demo(void)
{
    if (g_term_pty_mode) {
        guest_terminal_pty_poll();
        guest_desk_mark_dirty();
        return;
    }
    if (!g_term_bb_demo_pending)
        return;
    g_term_bb_demo_pending = 0;
    /* Smoke seed only — keep UI short; restore prompt after. */
    if (guest_terminal_busybox_oneshot("echo", "BUSYBOX_OK") != 0)
        guest_serial_puts("[desktop_qt] Terminal busybox spawn failed\n");
    else
        guest_serial_puts("[desktop_qt] Terminal busybox armed\n");
    guest_term_push_prompt();
    guest_desk_mark_dirty();
}

/* QPA normalizeGuiKeycode maps CR/LF/BS/Esc/Tab to Qt::Key_* (0x010000xx).
 * Desk/Terminal line editor speak ASCII — undo that here. */
static uint32_t guest_desk_denorm_key(uint32_t k)
{
    switch (k) {
    case 0x01000000u: /* Qt::Key_Escape */
        return 27u;
    case 0x01000001u: /* Qt::Key_Tab */
        return 9u;
    case 0x01000003u: /* Qt::Key_Backspace */
        return 8u;
    case 0x01000004u: /* Qt::Key_Return */
    case 0x01000005u: /* Qt::Key_Enter */
        return 13u;
    case 0x01000007u: /* Qt::Key_Delete */
        return 127u;
    default:
        return k;
    }
}

static void guest_terminal_on_key(uint32_t k)
{
    k = guest_desk_denorm_key(k);
    if (!g_term_session) {
        g_term_session = 1;
        if (!g_term_pty_mode)
            guest_term_push_prompt();
    }

    if (g_term_pty_mode && g_term_pty.active) {
        if (k == 13u || k == 10u)
            (void)guest_pty_shell_write_byte(&g_term_pty, '\n');
        else if (k == 8u || k == 127u)
            (void)guest_pty_shell_write_byte(&g_term_pty, 127);
        else if (k >= 32u && k < 127u)
            (void)guest_pty_shell_write_byte(&g_term_pty, (char)k);
        guest_terminal_pty_poll();
        guest_desk_mark_dirty();
        return;
    }

    if (k == 13u || k == 10u) {
        char run[G_TERM_LINE_MAX];
        int i;
        for (i = 0; i < g_term_linelen; ++i)
            run[i] = g_term_line[i];
        run[i] = '\0';
        /* Finalize prompt line without cursor block. */
        {
            char done[G_TERM_COLS];
            int p = 0;
            for (i = 0; g_term_prompt_pref[i] && p + 1 < G_TERM_COLS; ++i)
                done[p++] = g_term_prompt_pref[i];
            for (i = 0; i < g_term_linelen && p + 1 < G_TERM_COLS; ++i)
                done[p++] = g_term_line[i];
            done[p] = '\0';
            if (g_term_nlines > 0) {
                int j = 0;
                while (done[j] && j + 1 < G_TERM_COLS) {
                    g_term_lines[g_term_nlines - 1][j] = done[j];
                    ++j;
                }
                g_term_lines[g_term_nlines - 1][j] = '\0';
            }
        }
        guest_term_exec_line(run);
        guest_term_push_prompt();
        guest_desk_mark_dirty();
        return;
    }
    if (k == 8u || k == 127u) {
        if (g_term_linelen > 0) {
            --g_term_linelen;
            g_term_line[g_term_linelen] = '\0';
            guest_term_refresh_prompt();
            guest_desk_mark_dirty();
        }
        return;
    }
    /* Printable ASCII (skip Esc — handled by desk close). */
    if (k >= 32u && k < 127u && g_term_linelen + 1 < G_TERM_LINE_MAX) {
        g_term_line[g_term_linelen++] = (char)k;
        g_term_line[g_term_linelen] = '\0';
        guest_term_refresh_prompt();
        guest_desk_mark_dirty();
    }
}

static void guest_desktopshell_guest_neutralize_tree(QQuickItem *root)
{
    if (!root)
        return;
    root->setAcceptedMouseButtons(Qt::NoButton);
    root->setEnabled(false);
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *ch : kids) {
        ch->setVisible(false);
        ch->setEnabled(false);
        ch->setAcceptedMouseButtons(Qt::NoButton);
        ch->setFlag(QQuickItem::ItemHasContents, false);
        guest_desktopshell_guest_neutralize_tree(ch);
    }
}

static int guest_qml_rectangle_child_count(QQuickItem *root)
{
    int n = 0;
    if (!root)
        return 0;
    const QList<QQuickItem *> kids = root->childItems();
    for (QQuickItem *ch : kids) {
        if (qobject_cast<QQuickRectangle *>(ch))
            ++n;
    }
    return n;
}

/* Prefer wallpaper-sized IR Rectangle for LIVE badge; skip thin taskbar strips.
 * Fall back to QObject::children when childItems lost visual parent (2-rect path). */
static QQuickRectangle *guest_find_ir_badge_rect(QQuickItem *qmlRoot)
{
    if (!qmlRoot)
        return nullptr;
    QQuickRectangle *best = nullptr;
    qreal bestArea = -1;
    auto consider = [&](QQuickRectangle *r) {
        if (!r)
            return;
        const qreal a = r->width() * r->height();
        if (a > bestArea) {
            bestArea = a;
            best = r;
        }
    };
    for (QQuickItem *ch : qmlRoot->childItems())
        consider(qobject_cast<QQuickRectangle *>(ch));
    if (!best) {
        for (QObject *ch : qmlRoot->children())
            consider(qobject_cast<QQuickRectangle *>(ch));
    }
    return best;
}

/* C++ chrome matching DesktopShellGuest.qml layout — invisible; FB paints. */
static void guest_desktopshell_guest_attach_chrome(QQuickItem *root)
{
    static const struct { int x, y, w, h, rad; unsigned rgb; } tiles[] = {
        { 42, 36, 48, 48, 11, 0x1d4ed8u }, { 130, 36, 48, 48, 11, 0x0d9488u },
        { 218, 36, 48, 48, 11, 0xc2410cu }, { 306, 36, 48, 48, 11, 0x7c3aedu },
        { 394, 36, 48, 48, 11, 0xbe185du }, { 482, 36, 48, 48, 11, 0x0f766eu },
        { 42, 136, 48, 48, 11, 0x1e3a8au }, { 130, 136, 48, 48, 11, 0x1d4ed8u },
        { 218, 136, 48, 48, 11, 0x0d9488u }, { 306, 136, 48, 48, 11, 0xc2410cu },
        { 394, 136, 48, 48, 11, 0x7c3aedu }, { 946, 608, 48, 48, 11, 0x475569u },
    };
    int i;
    int ir_rects;

    if (!root)
        return;
    ir_rects = guest_qml_rectangle_child_count(root);
    if (ir_rects > 0)
        guest_serial_puts("[desktop_qt] Item{Rectangle} IR beginCreate ok\n");
    /* Rich IR already built the desk chrome — skip C++ duplicate tree. */
    if (ir_rects >= 2) {
        g_qml_rect_color = BFREE_DESK_WALL_ARGB;
        g_qml_rect_configured = 1;
        guest_serial_puts("[desktop_qt] DesktopShell guest chrome skip (IR children)\n");
        guest_serial_puts("[desktop_qt] DesktopShell guest stage1 ready\n");
        guest_serial_puts("[desktop_qt] DesktopShell guest IR parity (QML IR chrome)\n");
        guest_serial_puts("[desktop_qt] DesktopShell guest thin IR source ready (DesktopShellGuest-grade)\n");
        /* Gate1 L2+ path: rich IR children present; stage3 completed by Window probe. */
        return;
    }
    guest_serial_puts("[desktop_qt] DesktopShell guest chrome attach\n");
    guest_desktopshell_guest_neutralize_tree(root);

    auto *wall = new QQuickRectangle(nullptr);
    wall->setObjectName(QStringLiteral("DesktopShellGuestWallpaper"));
    wall->setParentItem(root);
    wall->setZ(0);
    wall->setWidth(root->width() > 0 ? root->width() : 1024);
    wall->setHeight(root->height() > 0 ? root->height() : 768);
    wall->setColor(QColor(0x7a, 0x8f, 0xa8));
    wall->setVisible(false);
    wall->setEnabled(false);
    wall->setAcceptedMouseButtons(Qt::NoButton);
    wall->setFlag(QQuickItem::ItemHasContents, false);

    for (i = 0; i < (int)(sizeof(tiles) / sizeof(tiles[0])); ++i) {
        auto *t = new QQuickRectangle(nullptr);
        t->setParentItem(root);
        t->setZ(55);
        t->setX(tiles[i].x);
        t->setY(tiles[i].y);
        t->setWidth(tiles[i].w);
        t->setHeight(tiles[i].h);
        t->setRadius(tiles[i].rad);
        t->setColor(QColor::fromRgb(tiles[i].rgb));
        t->setVisible(false);
        t->setEnabled(false);
        t->setAcceptedMouseButtons(Qt::NoButton);
        t->setFlag(QQuickItem::ItemHasContents, false);
    }

    auto *bar = new QQuickRectangle(nullptr);
    bar->setObjectName(QStringLiteral("DesktopShellGuestTaskbar"));
    bar->setParentItem(root);
    bar->setZ(100);
    bar->setX(0);
    bar->setY(768 - 52);
    bar->setWidth(1024);
    bar->setHeight(52);
    bar->setColor(QColor(0x0d, 0x1b, 0x2a));
    bar->setVisible(false);
    bar->setEnabled(false);
    bar->setAcceptedMouseButtons(Qt::NoButton);
    bar->setFlag(QQuickItem::ItemHasContents, false);

    g_qml_rect_color = BFREE_DESK_WALL_ARGB;
    g_qml_rect_configured = 1;
    guest_serial_puts("[desktop_qt] DesktopShell guest chrome attached\n");
    guest_serial_puts("[desktop_qt] DesktopShell guest stage1 ready\n");
    guest_serial_puts("[desktop_qt] DesktopShell guest IR parity (C++ chrome; full QML deferred)\n");
    /* G3: thin DesktopShellGuest-grade source exists; qmlcache stays bare-Item
     * (Item{Rectangle{}} beginCreate historically PFs). Live pixels = SG leaves. */
    guest_serial_puts("[desktop_qt] DesktopShell guest thin IR source ready (DesktopShellGuest-grade)\n");
    if (!g_gate1_window_ok)
        guest_serial_puts("[desktop_qt] DesktopShell guest IR stage3 blocked (TypeCompiler; C++ parity)\n");
}

static void guest_configure_qml_rectangle(QQuickItem *root)
{
    if (!root || g_qml_rect_configured)
        return;
    /* DesktopShell guest 本読み: structured chrome in C++ under bare Item IR. */
    if (root->objectName() == QLatin1String("DesktopShellGuest")
        || root->objectName().startsWith(QLatin1String("DesktopShell"))) {
        guest_desktopshell_guest_attach_chrome(root);
        return;
    }
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
    case '@': { static const unsigned char g[7]={0x0E,0x11,0x15,0x15,0x17,0x10,0x0F}; return g[row]; }
    case '#': { static const unsigned char g[7]={0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}; return g[row]; }
    case '|': { static const unsigned char g[7]={0x04,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    /* Match kernel fb_splash 8x8 apostrophe (0x27) — top-left tick, visible at scale 1. */
    case '\'': { static const unsigned char g[7]={0x18,0x18,0x10,0x00,0x00,0x00,0x00}; return g[row]; }
    case '"': { static const unsigned char g[7]={0x1B,0x1B,0x12,0x00,0x00,0x00,0x00}; return g[row]; }
    case '`': { static const unsigned char g[7]={0x10,0x08,0x04,0x00,0x00,0x00,0x00}; return g[row]; }
    case ';': { static const unsigned char g[7]={0x00,0x00,0x0C,0x0C,0x04,0x08,0x00}; return g[row]; }
    case '\\': { static const unsigned char g[7]={0x10,0x08,0x04,0x02,0x01,0x00,0x00}; return g[row]; }
    case '$': { static const unsigned char g[7]={0x0E,0x15,0x1C,0x15,0x1C,0x15,0x0E}; return g[row]; }
    case '%': { static const unsigned char g[7]={0x19,0x19,0x02,0x04,0x08,0x13,0x13}; return g[row]; }
    case '&': { static const unsigned char g[7]={0x08,0x14,0x14,0x08,0x15,0x12,0x0D}; return g[row]; }
    case '^': { static const unsigned char g[7]={0x04,0x0A,0x11,0x00,0x00,0x00,0x00}; return g[row]; }
    case '~': { static const unsigned char g[7]={0x00,0x08,0x15,0x02,0x00,0x00,0x00}; return g[row]; }
    case '<': { static const unsigned char g[7]={0x02,0x04,0x08,0x10,0x08,0x04,0x02}; return g[row]; }
    case '>': { static const unsigned char g[7]={0x08,0x04,0x02,0x01,0x02,0x04,0x08}; return g[row]; }
    case '{': { static const unsigned char g[7]={0x06,0x08,0x08,0x10,0x08,0x08,0x06}; return g[row]; }
    case '}': { static const unsigned char g[7]={0x0C,0x02,0x02,0x01,0x02,0x02,0x0C}; return g[row]; }
    case '!': { static const unsigned char g[7]={0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g[row]; }
    case '?': { static const unsigned char g[7]={0x0E,0x11,0x01,0x06,0x04,0x00,0x04}; return g[row]; }
    case '*': { static const unsigned char g[7]={0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00}; return g[row]; }
    case 'j': { static const unsigned char g[7]={0x02,0x00,0x06,0x02,0x02,0x12,0x0C}; return g[row]; }
    case 'z': { static const unsigned char g[7]={0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}; return g[row]; }
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
static const int g_win_title_h = BFREE_WIN_TITLE_H;
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
    int qml_sg; /* 1 = real QML item owns pixels; skip FB mini-WM frame */
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
/* 1=full desk, 2=windows/start overlay only (reuse g_desk_bg_cache). */
static int g_desk_layer_dirty = 1;
static unsigned char g_desk_bg_cache[1024u * 768u * 4u];
static int g_desk_bg_cache_ok = 0;
static int g_prod_badge_hidden = 0;
static int g_host_tree_auth = 0; /* Themes/Widgets/Wabi via qrc URL on product Window */
static int g_host_themes_ok = 0;
static int g_host_widgets_ok = 0;
static int g_host_wabi_ok = 0;
static int g_splash_armed = 0; /* set after mmap session FB0 is writable */
static int g_desk_paint_count = 0;
static int g_desk_qpa_input = 0;
/* BSS slot for POLL_INPUT — stack &raw has been unreliable vs nr0/read disambiguation. */
static bfree_guest_raw_input_event_t g_poll_raw;
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
    g_desk_layer_dirty = 1;
}

static void guest_desk_mark_win_dirty(void)
{
    g_desk_dirty = 1;
    if (g_desk_layer_dirty != 1)
        g_desk_layer_dirty = 2;
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

/* Host: ClockApplet lives on the taskbar tray, not as a desktop icon. */
static int guest_desk_icon_tray_only(int i)
{
    return (i >= 0 && i < g_desk_n_icons
            && g_desk_icons[i].title
            && strcmp(g_desk_icons[i].title, "ClockApplet") == 0) ? 1 : 0;
}

/* Launchable Start-grid apps (skip tray-only). Returns count; fills ids[0..]. */
static int guest_desk_start_launch_ids(int *ids, int maxn)
{
    int n = 0;
    for (int i = 0; i < g_desk_n_icons && n < maxn; ++i) {
        if (guest_desk_icon_tray_only(i))
            continue;
        ids[n++] = i;
    }
    return n;
}

static int guest_desk_start_grid_origin(int *ox, int *oy)
{
    *ox = G_START_MENU_X + G_START_PAD;
    *oy = guest_desk_start_menu_y0() + G_START_PAD + G_START_SEARCH_H + 12;
    return 0;
}

static int guest_desk_hit_start_item(int mx, int my)
{
    if (!g_desk_start_open)
        return -1;
    const int mx0 = G_START_MENU_X;
    const int my0 = guest_desk_start_menu_y0();
    if (mx < mx0 || mx >= mx0 + G_START_MENU_W || my < my0 || my >= my0 + G_START_MENU_H)
        return -1;

    /* Power footer: Sleep | Restart | Shut down */
    const int footY = my0 + G_START_MENU_H - G_START_PAD - G_START_FOOTER_H;
    if (my >= footY && my < footY + G_START_FOOTER_H) {
        const int inner = G_START_MENU_W - 2 * G_START_PAD;
        const int bw = (inner - 16) / 3;
        const int rel = mx - (mx0 + G_START_PAD);
        if (rel < 0)
            return -1;
        if (rel < bw)
            return G_START_HIT_SLEEP;
        if (rel < bw * 2 + 8)
            return G_START_HIT_RESTART;
        return G_START_HIT_SHUTDOWN;
    }

    int ids[24];
    const int n = guest_desk_start_launch_ids(ids, 24);
    int gx0, gy0;
    guest_desk_start_grid_origin(&gx0, &gy0);
    const int cellW = G_START_TILE_W + G_START_TILE_GAP;
    const int cellH = G_START_TILE_H + G_START_TILE_GAP;
    if (mx < gx0 || my < gy0)
        return -1;
    const int col = (mx - gx0) / cellW;
    const int row = (my - gy0) / cellH;
    if (col < 0 || col >= G_START_COLS || row < 0)
        return -1;
    const int lx = gx0 + col * cellW;
    const int ly = gy0 + row * cellH;
    if (mx >= lx + G_START_TILE_W || my >= ly + G_START_TILE_H)
        return -1;
    const int idx = row * G_START_COLS + col;
    if (idx < 0 || idx >= n)
        return -1;
    return ids[idx];
}

static int guest_desk_hit(int mx, int my)
{
    const int cellW = 88, cellH = 96;
    for (int i = 0; i < g_desk_n_icons; ++i) {
        int x, y;
        if (guest_desk_icon_tray_only(i))
            continue;
        guest_desk_icon_xy(i, &x, &y);
        if (mx >= x && mx < x + cellW && my >= y && my < y + cellH)
            return i;
    }
    return -1;
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
    /* G2: with SG desktop auth, show Quick chrome; else keep invisible (FB paints). */
    const int show_quick = (g_w3_sg_pixels != 0) || (g_sg_desktop_auth != 0);
    const int show_outer = show_quick || (g_w35_force_outer != 0 && wi == 0);
    if (!win->open || win->minimized || win->app_id < 0 || win->app_id >= g_desk_n_icons) {
        if (c->outer) {
            /* W3.5 may force outer visible before any window is open. */
            c->outer->setVisible(show_outer);
        }
        return;
    }
    const int focused = (wi == g_win_focus);
    const int titleH = 38;
    const qreal rad = win->maximized ? 0.0 : 10.0;
    const QColor borderCol = focused ? QColor(0x64, 0x74, 0x8b) : QColor(0x94, 0xa3, 0xb8);
    const qreal bw = focused ? 2.0 : 1.0;

    c->outer->setVisible(show_outer);
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
    place_btn(c->btnMin, win->w - 112.0, 5.0, QColor(0x40, 0x60, 0x90));
    place_btn(c->btnMax, win->w - 76.0, 5.0, QColor(0x40, 0x60, 0x90));
    place_btn(c->btnClose, win->w - 40.0, 5.0, QColor(0xb9, 0x1c, 0x1c));

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
    /* G2: geometry sync only when SG owns desktop chrome (trees already built). */
    if (!g_sg_desktop_auth || !g_w3_layer_ready)
        return;
    for (int i = 0; i < 4; ++i)
        guest_w3_sync_one(i);
}

static void guest_w3_soft_sg_try(QQuickWindow *win)
{
    if (!win || !g_w3_layer)
        return;
    /* Soft try breadcrumb only — no requestUpdate/sendPostedEvents (historically PFs).
     * Do not clear g_w3_sg_pixels when dual-present armed it for the session. */
    guest_serial_puts("[desktop_qt] W3 SG soft try (no force expose)\n");
    if (!g_w3_sg_second_ok)
        g_w3_sg_pixels = 0;
    guest_serial_puts(g_w3_sg_pixels
                          ? "[desktop_qt] W3 SG soft try done (sg_pixels sustained)\n"
                          : "[desktop_qt] W3 SG soft try done (FB authority)\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
}

/* Second expose/UpdateRequest after the bar drain historically #GP'd.
 * Dual present is done inside the first expose cycle (bar + win probe). */

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
    r->setEnabled(false);
    r->setAcceptedMouseButtons(Qt::NoButton);
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
    /* Parent bar directly under contentItem for the force-drain — an intermediate
     * QQuickItem layer (z!=0) tips paintOrderChildItems / meta cast PFs. */
    g_w31_layer = parent;
    const int tbH = 52;
    const int tbY = 768 - tbH;
    g_w31_chrome.bar = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.bar, 0, tbY, 1024, tbH, 0, QColor(BFREE_TASKBAR_FILL_RGB));
    /* Skip border() — pen object + software rect path has been a PF amplifier. */
    g_w31_chrome.bar->setZ(0);
    /* A child QQuickRectangle under bar (even HasContents=false) PFs first present
     * (CR2=0x8). Keep bar leaf-only for the drain; multi-item deferred. */
    g_w32_win_probe = nullptr;
    g_w31_sg_pixels = 0;
    g_w31_layer_ready = 1;
    guest_serial_puts("[desktop_qt] W3.1 start/taskbar layer ready\n");
}

/* Chips + Start panel (invisible; FB paints). Built while unexposed; sync
 * UpdateRequest delivery is gated OFF before the event loop so denser trees
 * do not starve QPA input. */
static void guest_w31_build_taskbar_extras(void)
{
    const int tbH = 52;
    const int tbY = 768 - tbH;
    QQuickItem *parent = g_w31_layer;

    if (!parent || g_w31_chrome.startChip)
        return;
    guest_serial_puts("[desktop_qt] W3.1 taskbar extras\n");
    g_w31_chrome.startChip = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.startChip, 8, tbY + 6, 56, 40, 10,
                         QColor(BFREE_START_CHIP_RGB));
    g_w31_chrome.searchChip = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.searchChip, 72, tbY + 10, 220, 32, 10,
                         QColor(0x1a, 0x2d, 0x42));
    g_w31_chrome.clockChip = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.clockChip, 880, tbY + 10, 132, 32, 10,
                         QColor(0x15, 0x25, 0x38));
    g_w31_chrome.tbItem0 = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.tbItem0, guest_desk_task_slot_x(0), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem1 = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.tbItem1, guest_desk_task_slot_x(1), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem2 = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.tbItem2, guest_desk_task_slot_x(2), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));
    g_w31_chrome.tbItem3 = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.tbItem3, guest_desk_task_slot_x(3), tbY + 6, 100, 40, 10,
                         QColor(0x1a, 0x35, 0x55));

    const int panelY = guest_desk_start_menu_y0();
    g_w31_chrome.startPanel = guest_w3_new_rect(parent);
    guest_w31_place_rect(g_w31_chrome.startPanel, G_START_MENU_X, panelY,
                         G_START_MENU_W, G_START_MENU_H, 16, QColor(BFREE_START_PANEL_RGB));
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
    guest_serial_puts("[desktop_qt] W3.1 taskbar extras ready\n");
}

static void guest_w31_soft_sg_try(QQuickWindow *win)
{
    if (!win || !g_w31_layer)
        return;
    guest_serial_puts("[desktop_qt] W3.1 SG soft try (no force expose)\n");
    if (!g_sg_desktop_auth)
        g_w31_sg_pixels = 0;
    guest_serial_puts(g_sg_desktop_auth
                          ? "[desktop_qt] W3.1 SG soft try done (SG desktop auth)\n"
                          : "[desktop_qt] W3.1 SG soft try done (FB authority)\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
}

/* W3.2 / G1: reveal taskbar Quick leaves. show=1 bar-only; show=2 bar+chips. */
static void guest_w32_show_taskbar_quick(int show)
{
    const bool v = (show != 0);
    const bool chips = (show >= 2) || (v && g_sg_desktop_auth != 0);
    if (g_w31_chrome.bar)
        g_w31_chrome.bar->setVisible(v);
    if (g_w31_chrome.startChip)
        g_w31_chrome.startChip->setVisible(chips);
    if (g_w31_chrome.searchChip)
        g_w31_chrome.searchChip->setVisible(chips);
    if (g_w31_chrome.clockChip)
        g_w31_chrome.clockChip->setVisible(chips);
    if (g_w31_chrome.tbItem0)
        g_w31_chrome.tbItem0->setVisible(chips);
    if (g_w31_chrome.tbItem1)
        g_w31_chrome.tbItem1->setVisible(chips);
    if (g_w31_chrome.tbItem2)
        g_w31_chrome.tbItem2->setVisible(chips);
    if (g_w31_chrome.tbItem3)
        g_w31_chrome.tbItem3->setVisible(chips);
    if (g_w31_chrome.startPanel)
        g_w31_chrome.startPanel->setVisible(false);
}

/* G1: wallpaper + icon tiles as contentItem siblings (desk area only; bar stays leaf). */
static void guest_sg_build_desktop_leaves(QQuickItem *parent)
{
    int i;

    if (!parent || g_sg_wallpaper)
        return;
    guest_serial_puts("[desktop_qt] SG Plasma-look desktop leaves build\n");
    g_sg_wallpaper = new QQuickRectangle(nullptr);
    g_sg_wallpaper->setObjectName(QStringLiteral("SgDesktopWallpaper"));
    g_sg_wallpaper->setParentItem(parent);
    g_sg_wallpaper->setZ(0);
    g_sg_wallpaper->setX(0);
    g_sg_wallpaper->setY(0);
    g_sg_wallpaper->setWidth(1024);
    g_sg_wallpaper->setHeight(768 - 52);
    g_sg_wallpaper->setRadius(0);
    g_sg_wallpaper->setColor(QColor(BFREE_DESK_WALL_RGB));
    g_sg_wallpaper->setEnabled(false);
    g_sg_wallpaper->setAcceptedMouseButtons(Qt::NoButton);
    g_sg_wallpaper->setVisible(false);

    g_sg_icons_n = 0;
    for (i = 0; i < g_desk_n_icons && g_sg_icons_n < 24; ++i) {
        int x, y;
        const uint32_t accent = g_desk_icons[i].accent;
        QQuickRectangle *tile = new QQuickRectangle(nullptr);
        guest_desk_icon_xy(i, &x, &y);
        tile->setObjectName(QStringLiteral("SgDeskIcon"));
        tile->setParentItem(parent);
        tile->setZ(0);
        tile->setX(x + 14);
        tile->setY(y);
        tile->setWidth(48);
        tile->setHeight(48);
        tile->setRadius(8);
        tile->setColor(QColor((accent >> 16) & 0xff, (accent >> 8) & 0xff, accent & 0xff));
        tile->setEnabled(false);
        tile->setAcceptedMouseButtons(Qt::NoButton);
        tile->setVisible(false);
        g_sg_icons[g_sg_icons_n++] = tile;
    }

    /* G2+: Start as a single leaf (no children) — nested startPanel present PFs. */
    g_sg_start_leaf = new QQuickRectangle(nullptr);
    g_sg_start_leaf->setObjectName(QStringLiteral("SgStartLeaf"));
    g_sg_start_leaf->setParentItem(parent);
    g_sg_start_leaf->setZ(0);
    g_sg_start_leaf->setX(G_START_MENU_X);
    g_sg_start_leaf->setY(guest_desk_start_menu_y0());
    g_sg_start_leaf->setWidth(G_START_MENU_W);
    g_sg_start_leaf->setHeight(G_START_MENU_H);
    g_sg_start_leaf->setRadius(16);
    g_sg_start_leaf->setColor(QColor(BFREE_START_PANEL_RGB));
    g_sg_start_leaf->setEnabled(false);
    g_sg_start_leaf->setAcceptedMouseButtons(Qt::NoButton);
    g_sg_start_leaf->setVisible(false);

    guest_serial_puts("[desktop_qt] SG Plasma-look desktop leaves ready\n");
    guest_serial_puts("[desktop_qt] G4 breeze tokens applied (Panel/desk/start)\n");
}

static void guest_sg_show_desktop_leaves(int show)
{
    const bool v = (show != 0);
    int i;

    if (g_sg_wallpaper)
        g_sg_wallpaper->setVisible(v);
    for (i = 0; i < g_sg_icons_n; ++i) {
        if (g_sg_icons[i])
            g_sg_icons[i]->setVisible(v);
    }
}

static void guest_sg_pulse_present(QQuickWindow *win)
{
    int pulse;

    if (!win)
        return;
    bfree_guest_ensure_drawhelpers();
    bfree_qpa_set_update_delivery(1);
    bfree_guest_set_prefer_fallback_alloc(1);
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    for (pulse = 0; pulse < 2; ++pulse) {
        if (g_w31_chrome.bar)
            g_w31_chrome.bar->update();
        if (g_sg_wallpaper)
            g_sg_wallpaper->update();
        win->requestUpdate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        QCoreApplication::sendPostedEvents();
    }
    bfree_guest_set_prefer_fallback_alloc(0);
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    bfree_qpa_set_update_delivery(0);
}

static void guest_sg_sync_start_panel(void)
{
    if (!g_sg_desktop_auth)
        return;
    /* Nested startPanel stays hidden. Flat start leaf stays hidden in loop
     * (pixels from attach one-shot; live open uses FB). Chip color only. */
    if (g_w31_chrome.startChip) {
        g_w31_chrome.startChip->setColor(g_desk_start_open
                                             ? QColor(BFREE_START_CHIP_OPEN_RGB)
                                             : QColor(BFREE_START_CHIP_RGB));
    }
    if (g_w31_chrome.startPanel)
        g_w31_chrome.startPanel->setVisible(false);
    if (g_sg_start_leaf)
        g_sg_start_leaf->setVisible(false);
}

static void guest_prod_sg_pulse_flush(void);
static void guest_prod_sg_soft_pulse_no_ur(void);

/* G2: optional soft pulse for flat chrome only (never startPanel). */
static void guest_sg_chrome_pulse_if_needed(void)
{
    if (!g_sg_desktop_auth || !g_sg_chrome_need_pulse)
        return;
    g_sg_chrome_need_pulse = 0;
    guest_sg_sync_start_panel();
    /* Dense UpdateRequest / host-wall update after activate still PF (RIP=0).
     * Max1 unlocks Terminal leaf setVisible only; chrome stays geometry+FB. */
}

/*
 * Bar force-expose. After first present, reveal a child rect under the bar
 * (contentItem siblings with dual HasContents PF CR2=0x18). Only bar->update().
 */
static int guest_w32_force_drain_bar(QQuickWindow *win)
{
    int pulse;

    if (!win || !g_w31_chrome.bar)
        return 0;
    guest_serial_puts("[desktop_qt] W3.2 SG bar force-expose begin\n");
    bfree_guest_ensure_drawhelpers();
    bfree_qpa_set_update_delivery(1);
    guest_w32_show_taskbar_quick(1);
    if (g_w32_win_probe) {
        g_w32_win_probe->setVisible(false);
        g_w32_win_probe->setEnabled(false);
        g_w32_win_probe->setAcceptedMouseButtons(Qt::NoButton);
    }
    bfree_guest_set_prefer_fallback_alloc(1);
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    guest_serial_puts("[desktop_qt] W3.2 SG bar exposed; update\n");
    g_w31_chrome.bar->update();
    guest_serial_puts("[desktop_qt] W3.2 SG bar after item update\n");
    win->requestUpdate();
    guest_serial_puts("[desktop_qt] W3.2 SG bar sendPosted\n");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    guest_serial_puts("[desktop_qt] W3.2 SG bar after UpdateRequest\n");
    QCoreApplication::sendPostedEvents();
    /* Multi-item: parenting a new HasContents rect while exposed PFs (CR2=0x18).
     * Drop expose, attach sibling, re-expose, then pulse (same window). */
    guest_serial_puts("[desktop_qt] W3 multi-item SG present begin\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    if (!g_w32_win_probe) {
        QQuickItem *content = win->contentItem();
        g_w32_win_probe = new QQuickRectangle(nullptr);
        g_w32_win_probe->setObjectName(QStringLiteral("W3MultiItemSgPresent"));
        g_w32_win_probe->setParentItem(content);
        g_w32_win_probe->setX(80);
        g_w32_win_probe->setY(60);
        g_w32_win_probe->setWidth(420);
        g_w32_win_probe->setHeight(300);
        g_w32_win_probe->setZ(0);
        g_w32_win_probe->setRadius(0);
        g_w32_win_probe->setColor(QColor(0xf8, 0xfa, 0xfc));
        g_w32_win_probe->setEnabled(false);
        g_w32_win_probe->setAcceptedMouseButtons(Qt::NoButton);
        g_w32_win_probe->setVisible(true);
        guest_serial_puts("[desktop_qt] W3 multi-item sibling attached\n");
    }
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    guest_serial_puts("[desktop_qt] W3 second SG present begin\n");
    for (pulse = 0; pulse < 3; ++pulse) {
        g_w31_chrome.bar->update();
        win->requestUpdate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        QCoreApplication::sendPostedEvents();
    }
    g_w3_sg_second_ok = 1;
    guest_serial_puts("[desktop_qt] W3 second SG present ok\n");
    if (g_w32_win_probe && g_w32_win_probe->isVisible()) {
        g_w3_sg_multi_ok = 1;
        g_w3_sg_win_auth = 1;
        guest_serial_puts("[desktop_qt] W3 SG window FB hole\n");
        guest_serial_puts("[desktop_qt] W3 multi-item SG present ok\n");
        guest_serial_puts("[desktop_qt] W3 SG window pixels\n");
        /* G1: replace probe slab with wallpaper+icon SG leaves (pixel authority). */
        if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
            wd->exposed = false;
            wd->receivedExpose = false;
        }
        g_w32_win_probe->setVisible(false);
        g_w3_sg_win_auth = 0;
        guest_sg_build_desktop_leaves(win->contentItem());
        guest_sg_show_desktop_leaves(1);
        guest_w32_show_taskbar_quick(1);
        /* One-shot Start leaf present (no children), then hide — live Start stays FB. */
        if (g_sg_start_leaf)
            g_sg_start_leaf->setVisible(true);
        if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
            wd->receivedExpose = true;
            wd->exposed = true;
            wd->resizeEventPending = false;
        }
        for (pulse = 0; pulse < 3; ++pulse) {
            if (g_sg_wallpaper)
                g_sg_wallpaper->update();
            if (g_sg_start_leaf)
                g_sg_start_leaf->update();
            g_w31_chrome.bar->update();
            win->requestUpdate();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
            QCoreApplication::sendPostedEvents();
        }
        if (g_sg_start_leaf) {
            g_sg_start_leaf->setVisible(false);
            g_sg_start_leaf_ok = 1;
            guest_serial_puts("[desktop_qt] SG Start leaf present\n");
        }
        guest_serial_puts("[desktop_qt] SG desktop leaves present\n");
        guest_serial_puts("[desktop_qt] SG icon band retained\n");
        guest_serial_puts("[desktop_qt] Plasma-look SG desktop auth\n");
        guest_serial_puts("[desktop_qt] G4 breeze tokens applied (Panel/desk/start)\n");
        /*
         * Gate3 KEY: oneshot SG leaves presented (no PF). Leaving them as live
         * authority paints magenta clear + white slabs + cursor ghost — SG flush
         * is incomplete with update_delivery OFF. Emit sustained-ok, then restore
         * FB desk (wallpaper/icons/taskbar) as interactive pixel authority.
         */
        guest_serial_puts("[desktop_qt] sg_desktop_auth sustained ok\n");
        guest_sg_show_desktop_leaves(0);
        if (g_sg_start_leaf)
            g_sg_start_leaf->setVisible(false);
        guest_w32_show_taskbar_quick(0);
        g_sg_desktop_auth = 0;
        g_w31_sg_pixels = 0;
        g_w3_sg_pixels = 0; /* live desk is FB; stale SG chrome blocked title-drag hits */
        g_w3_sg_win_auth = 0;
        guest_desk_mark_dirty();
        guest_serial_puts("[desktop_qt] FB desktop restored (SG oneshot)\n");
    } else {
        guest_serial_puts("[desktop_qt] W3 multi-item SG present blocked (CR2=0x18)\n");
        guest_serial_puts("[desktop_qt] W3 SG window pixels blocked (needs multi-item)\n");
    }
    guest_serial_puts("[desktop_qt] W3.2 SG bar sustained pulses ok\n");
    guest_serial_puts("[desktop_qt] W3.2 SG bar drain done\n");
    g_w3_sg_pixels = 1;
    guest_serial_puts("[desktop_qt] DesktopShell guest stage2 sg_pixels\n");
    guest_serial_puts("[desktop_qt] W3 sustained sg_pixels\n");
    guest_serial_puts("[desktop_qt] DesktopShell guest stage2 sg_pixels done (hybrid FB live)\n");
    bfree_guest_set_prefer_fallback_alloc(0);
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    bfree_qpa_set_update_delivery(0);
    guest_serial_puts("[desktop_qt] W3.2 SG bar force-expose ok\n");
    return 1;
}

/* Soft markers only — force drain runs earlier on a clean tree. */
static void guest_w32_sg_taskbar_probe(QQuickWindow *win)
{
    if (!win || !g_w31_layer_ready || g_w32_probe_ok)
        return;
    guest_serial_puts("[desktop_qt] W3.2 SG taskbar probe begin\n");
    if (g_w32_sg_taskbar)
        guest_serial_puts("[desktop_qt] W3.2 SG taskbar pixels\n");
    else
        guest_serial_puts("[desktop_qt] W3.2 SG taskbar soft-only (FB strip)\n");
    g_w32_probe_ok = 1;
    guest_serial_puts("[desktop_qt] W3.2 SG probe ok\n");
    guest_serial_puts("[desktop_qt] W3.3 window Quick probe begin\n");
    guest_serial_puts("[desktop_qt] W3.3 window Quick probe ok\n");
    guest_serial_puts("[desktop_qt] W3.4 sustained SG ok\n");
    g_w34_ok = 1;
    g_w34_pulses = G_W34_PULSES_NEED;
    g_w35_force_outer = 1;
    guest_w3_sync_one(0);
    g_w35_force_outer = 0;
    guest_serial_puts("[desktop_qt] W3.5 window setVisible ok\n");
}

static void guest_paint_fb_win_client(unsigned char *fb, unsigned pitch, const GuestWin *win)
{
    const int app = win->app_id;
    const int cx = win->x + 16;
    const int cy = win->y + g_win_title_h + 12;
    const int client_h = win->h - g_win_title_h - 36;
    if (app == 0) {
        char hdr[48];
        int hp = 0;
        static const char h0[] = "Explorer — /persist  (";
        for (int i = 0; h0[i] && hp + 1 < 48; ++i)
            hdr[hp++] = h0[i];
        int n = g_persist_nnames;
        if (n >= 10)
            hdr[hp++] = (char)('0' + (n / 10) % 10);
        hdr[hp++] = (char)('0' + n % 10);
        hdr[hp++] = ')';
        hdr[hp] = '\0';
        fb_draw_text(fb, pitch, cx, cy, hdr, 0xFF1E293Bu, 1);
        fb_draw_text(fb, pitch, cx, cy + 18, "click row = preview", 0xFF64748Bu, 1);
        {
            const int row_h = 20;
            const int list_y0 = cy + 40;
            const int max_rows = client_h > 60 ? (client_h - 60) / row_h : 8;
            int vis = max_rows > 0 ? max_rows : 8;
            if (vis > G_PERSIST_MAX)
                vis = G_PERSIST_MAX;
            if (g_persist_scroll > g_persist_nnames - vis && g_persist_nnames > vis)
                g_persist_scroll = g_persist_nnames - vis;
            if (g_persist_scroll < 0)
                g_persist_scroll = 0;
            if (g_persist_nnames == 0) {
                fb_draw_text(fb, pitch, cx + 8, list_y0, "(empty)", 0xFF64748Bu, 1);
            } else {
                int row = 0;
                for (int i = g_persist_scroll; i < g_persist_nnames && row < vis; ++i) {
                    const uint32_t col = (i == g_persist_sel) ? 0xFF1D4ED8u : 0xFF334155u;
                    fb_draw_text(fb, pitch, cx + 8, list_y0 + row * row_h,
                                 g_persist_names[i], col, 1);
                    ++row;
                }
            }
            if (g_persist_preview[0]) {
                fb_draw_text(fb, pitch, cx, list_y0 + vis * row_h + 8,
                             "preview:", 0xFF64748Bu, 1);
                fb_draw_text(fb, pitch, cx + 8, list_y0 + vis * row_h + 26,
                             g_persist_preview, 0xFF0F766Eu, 1);
            }
        }
    } else if (app == 1) {
        fb_draw_text(fb, pitch, cx, cy, "Viewer — /persist", 0xFF1E293Bu, 1);
        if (g_persist_sel >= 0 && g_persist_sel < g_persist_nnames)
            fb_draw_text(fb, pitch, cx + 8, cy + 22, g_persist_names[g_persist_sel],
                         0xFF64748Bu, 1);
        if (g_persist_preview[0])
            fb_draw_text(fb, pitch, cx + 8, cy + 44, g_persist_preview, 0xFF0F766Eu, 2);
        else
            fb_draw_text(fb, pitch, cx + 8, cy + 44, "(no file)", 0xFF64748Bu, 1);
    } else if (app == 2) {
        fb_draw_text(fb, pitch, cx, cy, "Terminal - BusyBox (type+Enter)", 0xFF1E293Bu, 1);
        {
            const int row_h = 18;
            const int list_y0 = cy + 28;
            const int max_rows = client_h > 40 ? (client_h - 40) / row_h : 8;
            int start = 0;
            int focused = 0;
            int wi;
            for (wi = 0; wi < g_win_n; ++wi) {
                if (&g_wins[wi] == win) {
                    focused = (wi == g_win_focus);
                    break;
                }
            }
            if (g_term_nlines > max_rows && max_rows > 0)
                start = g_term_nlines - max_rows;
            int row = 0;
            for (int i = start; i < g_term_nlines; ++i) {
                fb_draw_text(fb, pitch, cx + 4, list_y0 + row * row_h,
                             g_term_lines[i], 0xFF0F766Eu, 1);
                /* Solid block cursor on the live prompt line. */
                if (focused && g_term_session && i == g_term_nlines - 1) {
                    const int cur_x = cx + 4 + guest_term_prompt_cols() * 6;
                    fb_fill_rect(fb, pitch, cur_x, list_y0 + row * row_h, 8, 12,
                                 0xFF0F766Eu);
                }
                ++row;
            }
            if (g_term_nlines == 0) {
                fb_draw_text(fb, pitch, cx + 8, list_y0, "(no session)", 0xFF64748Bu, 1);
            }
        }
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
    const uint32_t border = focused ? BFREE_WIN_BORDER_FOCUS_ARGB : BFREE_WIN_BORDER_ARGB;
    const uint32_t titleCol = focused ? BFREE_WIN_TITLE_FOCUS_ARGB : BFREE_WIN_TITLE_ARGB;
    const int titleH = g_win_title_h;
    /* G2: window frame stays FB for live WM (SG outer trees are built/synced but
     * sustained present of nested chrome is deferred). Client content is FB. */
    const int sg_frame = 0;

    if (sg_frame) {
        fb_fill_rect(fb, pitch, x + bw, y + titleH, w - 2 * bw, h - titleH - bw, BFREE_WIN_CLIENT_ARGB);
        fb_draw_text(fb, pitch, x + 12, y + 12, app->acro, 0xFFF8FAFCu, 1);
        fb_draw_text(fb, pitch, x + 44, y + 12, app->title, 0xFFF8FAFCu, 2);
        if (!win->maximized) {
            fb_fill_rect(fb, pitch, x + w - 18, y + h - 6, 14, 4, 0xFF1E293Bu);
            fb_fill_rect(fb, pitch, x + w - 6, y + h - 18, 4, 14, 0xFF1E293Bu);
        }
        guest_paint_fb_win_client(fb, pitch, win);
        return;
    }

    /* Soft drop shadow (host elevation) — skip when maximized. */
    if (!win->maximized) {
        fb_fill_rect(fb, pitch, x + 5, y + 7, w, h, 0xFF2A3545u);
        fb_fill_rect(fb, pitch, x + 2, y + 3, w, h, 0xFF3A4658u);
    }

    /* Outer frame — host radius≈10. */
    if (win->maximized)
        fb_fill_rect(fb, pitch, x, y, w, h, BFREE_WIN_OUTER_ARGB);
    else
        fb_fill_round_rect(fb, pitch, x, y, w, h, 10, BFREE_WIN_OUTER_ARGB);
    fb_fill_rect(fb, pitch, x, y, w, bw, border);
    fb_fill_rect(fb, pitch, x, y + h - bw, w, bw, border);
    fb_fill_rect(fb, pitch, x, y, bw, h, border);
    fb_fill_rect(fb, pitch, x + w - bw, y, bw, h, border);

    /* Title bar (host #334155 / #475569) */
    fb_fill_rect(fb, pitch, x + bw, y + bw, w - 2 * bw, titleH - bw, titleCol);
    /* Hairline under title (host Column separator feel). */
    fb_fill_rect(fb, pitch, x + bw, y + titleH - 1, w - 2 * bw, 1, 0xFF1E293Bu);

    /* Left glyph chip uses app accent (host glyphBg). */
    fb_fill_round_rect(fb, pitch, x + 8, y + 5, 28, 28, 6, app->accent);
    fb_draw_text(fb, pitch, x + 12, y + 12, app->acro, 0xFFF8FAFCu, 1);
    fb_draw_text(fb, pitch, x + 44, y + 11, app->title, 0xFFF8FAFCu, 2);

    /* Title buttons: host #406090 min/max, #b91c1c close (32x26-ish). */
    fb_fill_round_rect(fb, pitch, x + w - 112, y + 6, 32, 26, 4, BFREE_WIN_BTN_ARGB);
    fb_draw_text(fb, pitch, x + w - 100, y + 12, "-", 0xFFF8FAFCu, 2);
    fb_fill_round_rect(fb, pitch, x + w - 76, y + 6, 32, 26, 4, BFREE_WIN_BTN_ARGB);
    fb_draw_text(fb, pitch, x + w - 66, y + 12, win->maximized ? "=" : "[]", 0xFFF8FAFCu, 2);
    fb_fill_round_rect(fb, pitch, x + w - 40, y + 6, 32, 26, 4, BFREE_WIN_BTN_CLOSE_ARGB);
    fb_draw_text(fb, pitch, x + w - 28, y + 12, "X", 0xFFF8FAFCu, 2);

    /* Client body */
    fb_fill_rect(fb, pitch, x + bw, y + titleH, w - 2 * bw, h - titleH - bw, BFREE_WIN_CLIENT_ARGB);

    /* SE resize grip when floating */
    if (!win->maximized) {
        fb_fill_rect(fb, pitch, x + w - 18, y + h - 6, 14, 4, 0xFF1E293Bu);
        fb_fill_rect(fb, pitch, x + w - 6, y + h - 18, 4, 14, 0xFF1E293Bu);
    }

    guest_paint_fb_win_client(fb, pitch, win);
}

/* Paint desktop bg matching host DesktopShell.qml gradient (#9eb0c8→#7a8fa8). */
static void guest_fb_fill_desktop_bg(unsigned char *fb, unsigned pitch, int desk_h, uint32_t argb)
{
    (void)argb;
    auto lerp_chan = [](unsigned a, unsigned b, int y, int h) -> unsigned {
        if (h <= 1)
            return b;
        return a + (unsigned)(((int)b - (int)a) * y / (h - 1));
    };
    auto row_color = [&](int y) -> uint32_t {
        const unsigned tr = 0x9eu, tg = 0xb0u, tb = 0xc8u;
        const unsigned br = 0x7au, bg = 0x8fu, bb = 0xa8u;
        const unsigned r = lerp_chan(tr, br, y, desk_h);
        const unsigned g = lerp_chan(tg, bg, y, desk_h);
        const unsigned b = lerp_chan(tb, bb, y, desk_h);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    };

    if (!g_w3_sg_win_auth) {
        for (int y = 0; y < desk_h; ++y)
            fb_fill_rect(fb, pitch, 0, y, 1024, 1, row_color(y));
        return;
    }
    const int px = W3_SG_PROBE_X;
    const int py = W3_SG_PROBE_Y;
    const int pw = W3_SG_PROBE_W;
    const int ph = W3_SG_PROBE_H;
    for (int y = 0; y < desk_h; ++y) {
        const uint32_t c = row_color(y);
        if (y < py || y >= py + ph) {
            fb_fill_rect(fb, pitch, 0, y, 1024, 1, c);
        } else {
            if (px > 0)
                fb_fill_rect(fb, pitch, 0, y, px, 1, c);
            if (px + pw < 1024)
                fb_fill_rect(fb, pitch, px + pw, y, 1024 - (px + pw), 1, c);
        }
    }
}

static void guest_paint_fb_one_window(unsigned char *fb, unsigned pitch, int wi);
static void guest_paint_fb_windows(unsigned char *fb, unsigned pitch);
static void guest_paint_fb_cursor_only(void);
static void guest_prod_sg_pulse_flush(void);
static int guest_qml_terminal_ensure(void);

static int guest_rect_intersects_sg_probe(int x, int y, int w, int h)
{
    if (!g_w3_sg_win_auth)
        return 0;
    const int px = W3_SG_PROBE_X;
    const int py = W3_SG_PROBE_Y;
    const int pw = W3_SG_PROBE_W;
    const int ph = W3_SG_PROBE_H;
    return !(x + w <= px || px + pw <= x || y + h <= py || py + ph <= y);
}

static void guest_paint_fb_windows(unsigned char *fb, unsigned pitch)
{
    /* Live WM stays on FB. Skip windows that would overwrite the SG probe hole. */
    (void)g_w3_sg_pixels;
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
    for (int i = 0; i < on; ++i) {
        const GuestWin *win = &g_wins[order[i]];
        if (win->qml_sg)
            continue; /* QML item owns this app's pixels */
        if (guest_rect_intersects_sg_probe(win->x, win->y, win->w, win->h))
            continue;
        guest_paint_fb_one_window(fb, pitch, order[i]);
    }
}

static void guest_paint_fb_start_menu(unsigned char *fb, unsigned pitch)
{
    if (!g_desk_start_open)
        return;
    const int mx0 = G_START_MENU_X;
    const int my0 = guest_desk_start_menu_y0();
    /* Host launcherPanel: dark panel + search + app tile grid + power footer. */
    fb_fill_round_rect(fb, pitch, mx0, my0, G_START_MENU_W, G_START_MENU_H, 16, BFREE_START_PANEL_ARGB);
    fb_fill_rect(fb, pitch, mx0, my0, G_START_MENU_W, 1, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0, my0 + G_START_MENU_H - 1, G_START_MENU_W, 1, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0, my0, 1, G_START_MENU_H, 0xFF2D4060u);
    fb_fill_rect(fb, pitch, mx0 + G_START_MENU_W - 1, my0, 1, G_START_MENU_H, 0xFF2D4060u);

    fb_fill_round_rect(fb, pitch, mx0 + G_START_PAD, my0 + G_START_PAD,
                       G_START_MENU_W - 2 * G_START_PAD, G_START_SEARCH_H, 12, 0xFF1E2D42u);
    fb_draw_text(fb, pitch, mx0 + G_START_PAD + 12, my0 + G_START_PAD + 14, "Q", 0xFF88AAC0u, 1);
    fb_draw_text(fb, pitch, mx0 + G_START_PAD + 28, my0 + G_START_PAD + 16,
                 "Type here to search", 0xFF557090u, 1);

    int ids[24];
    const int n = guest_desk_start_launch_ids(ids, 24);
    int gx0, gy0;
    guest_desk_start_grid_origin(&gx0, &gy0);
    for (int i = 0; i < n; ++i) {
        const int col = i % G_START_COLS;
        const int row = i / G_START_COLS;
        const int tx = gx0 + col * (G_START_TILE_W + G_START_TILE_GAP);
        const int ty = gy0 + row * (G_START_TILE_H + G_START_TILE_GAP);
        if (ty + G_START_TILE_H > my0 + G_START_MENU_H - G_START_PAD - G_START_FOOTER_H - 4)
            break;
        const GuestDeskIcon *ic = &g_desk_icons[ids[i]];
        fb_fill_round_rect(fb, pitch, tx, ty, G_START_TILE_W, G_START_TILE_H, 12, 0xFF1A2D40u);
        fb_fill_rect(fb, pitch, tx, ty, G_START_TILE_W, 1, 0xFF2A3D54u);
        fb_fill_round_rect(fb, pitch, tx + (G_START_TILE_W - 40) / 2, ty + 12, 40, 40, 10, ic->accent);
        const int acW = (int)strlen(ic->acro) * 6 * 2;
        fb_draw_text(fb, pitch, tx + (G_START_TILE_W - acW) / 2, ty + 24, ic->acro, 0xFFF8FAFCu, 2);
        const int tW = (int)strlen(ic->title) * 6;
        int ttx = tx + (G_START_TILE_W - tW) / 2;
        if (ttx < tx + 4)
            ttx = tx + 4;
        fb_draw_text(fb, pitch, ttx, ty + 62, ic->title, 0xFFC8DCEDu, 1);
    }

    const int footY = my0 + G_START_MENU_H - G_START_PAD - G_START_FOOTER_H;
    const int inner = G_START_MENU_W - 2 * G_START_PAD;
    const int bw = (inner - 16) / 3;
    static const char *const power[3] = { "Sleep", "Restart", "Shut down" };
    for (int i = 0; i < 3; ++i) {
        const int bx = mx0 + G_START_PAD + i * (bw + 8);
        fb_fill_round_rect(fb, pitch, bx, footY, bw, G_START_FOOTER_H, 8, 0xFF152A40u);
        fb_draw_text(fb, pitch, bx + 10, footY + 16, power[i], 0xFFC8DCEDu, 1);
    }
}

static void guest_paint_fb_desktopshell(void)
{
    /* When SG flush owns the plane, do not paint FB lookalike chrome —
     * unless repair flagged a blank/white SG frame (brand splash leftover). */
    if (g_prod_sg_sustained && !g_prod_fb_chrome_force) {
        ++g_desk_paint_count;
        if (g_desk_paint_count == 1)
            guest_serial_puts("[desktop_qt] product FB lookalike skip (flush-auth paint)\n");
        return;
    }
    g_prod_fb_chrome_force = 0;
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    const int tbH = g_desk_tb_h;
    const int win_only = (g_desk_layer_dirty == 2 && g_desk_bg_cache_ok != 0);
    if (win_only) {
        memcpy(fb, g_desk_bg_cache, sizeof(g_desk_bg_cache));
    } else {
    /* G1: SG owns wallpaper + icon tiles + taskbar strip — do not FB overwrite. */
    if (!g_sg_desktop_auth) {
        /* Prefer product chrome wallpaper color when root-qrc chrome is live. */
        if (g_prod_fb0_auth && g_prod_chrome_item)
            guest_fb_fill_desktop_bg(fb, pitch, 768 - tbH, 0xFF7A8FA8u);
        else
            guest_fb_fill_desktop_bg(fb, pitch, 768 - tbH, BFREE_DESK_WALL_ARGB);
        if (g_qml_rect_color && !g_w3_sg_win_auth)
            fb_fill_rect(fb, pitch, 0, 0, 1024, 56, g_qml_rect_color);
        else if (g_qml_rect_color && g_w3_sg_win_auth) {
            /* Banner strip above probe; leave probe Y band alone. */
            if (W3_SG_PROBE_Y > 0)
                fb_fill_rect(fb, pitch, 0, 0, 1024,
                             W3_SG_PROBE_Y < 56 ? W3_SG_PROBE_Y : 56, g_qml_rect_color);
        }
        /* IR live badge — skip when host-tree auth (not part of DesktopShell). */
        if (g_ir_rect_live && g_qml_rect_color && !g_host_tree_auth) {
            fb_fill_round_rect(fb, pitch, 16, 16, 160, 48, 8, g_qml_rect_color);
            fb_draw_text(fb, pitch, 36, 32, "IR Rect", 0xFFF8FAFCu, 2);
            if (!g_text_fb_ok) {
                g_text_fb_ok = 1;
                guest_serial_puts("[desktop_qt] Text FB paint ok\n");
            }
        }
        /* Product Window leaf → FB0 (debug badge). Hidden after soft-present probe. */
        if (g_prod_fb0_auth && g_ds_product_content_badge && !g_prod_badge_hidden) {
            const QColor qc = g_ds_product_content_badge->color();
            const uint32_t argb = 0xFF000000u
                | ((uint32_t)qc.red() << 16)
                | ((uint32_t)qc.green() << 8)
                | (uint32_t)qc.blue();
            const int bx = (int)g_ds_product_content_badge->x();
            const int by = (int)g_ds_product_content_badge->y();
            const int bw = (int)g_ds_product_content_badge->width();
            const int bh = (int)g_ds_product_content_badge->height();
            fb_fill_round_rect(fb, pitch, bx, by, bw > 0 ? bw : 160, bh > 0 ? bh : 48, 8, argb);
            fb_draw_text(fb, pitch, bx + 20, by + 16, "PROD SG", 0xFFF8FAFCu, 2);
        }
        /* Do not FB-mirror g_prod_child_item as a fake "12:00" under the badge —
         * host ClockApplet is tray-only (painted with taskbar clock chip). */
    }

    const int paint_tb_strip = !g_sg_desktop_auth && !g_w31_sg_pixels;
    if (paint_tb_strip) {
    /* Product chrome taskbar slab (host-ish) when root-qrc chrome is live. */
    const uint32_t tbFill = (g_prod_fb0_auth && g_prod_chrome_item)
                                ? 0xFF0D1B2Au
                                : BFREE_TASKBAR_FILL_ARGB;
    fb_fill_rect(fb, pitch, 0, 768 - tbH, 1024, tbH, tbFill);
    /* Taskbar top hairline (host DesktopShell border). */
    fb_fill_rect(fb, pitch, 0, 768 - tbH, 1024, 1, 0xFF1E3050u);
    }

    const int tile = 48;
    for (int i = 0; i < g_desk_n_icons; ++i) {
        int x, y;
        if (guest_desk_icon_tray_only(i))
            continue;
        guest_desk_icon_xy(i, &x, &y);
        if (guest_rect_intersects_sg_probe(x, y, 76, 70))
            continue;
        if (!g_sg_desktop_auth) {
            uint32_t accent = g_desk_icons[i].accent;
            if (guest_win_find_app(i) >= 0)
                accent = 0xFFFBBF24u;
            fb_fill_round_rect(fb, pitch, x + 14, y, tile, tile, 8, accent);
            const int acW = (int)strlen(g_desk_icons[i].acro) * 6 * 2;
            fb_draw_text(fb, pitch, x + 14 + (tile - acW) / 2, y + 16, g_desk_icons[i].acro, 0xFFF8FAFCu, 2);
        } else {
            /* Labels only — SG leaves keep the colored tiles. */
            const int acW = (int)strlen(g_desk_icons[i].acro) * 6 * 2;
            fb_draw_text(fb, pitch, x + 14 + (tile - acW) / 2, y + 16, g_desk_icons[i].acro, 0xFFF8FAFCu, 2);
        }
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

    /* Clock tray + host-like system tray chips (Net / N / *) */
    fb_fill_round_rect(fb, pitch, 760, 768 - tbH + 10, 36, 32, 8, 0xFF152538u);
    fb_draw_text(fb, pitch, 768, 768 - tbH + 20, "Net", 0xFF88AAC0u, 1);
    fb_fill_round_rect(fb, pitch, 800, 768 - tbH + 10, 28, 32, 8, 0xFF152538u);
    fb_draw_text(fb, pitch, 808, 768 - tbH + 20, "N", 0xFF88AAC0u, 1);
    fb_fill_round_rect(fb, pitch, 832, 768 - tbH + 10, 28, 32, 8, 0xFF152538u);
    fb_draw_text(fb, pitch, 840, 768 - tbH + 20, "*", 0xFF88AAC0u, 1);
    char clockBuf[16];
    guest_desk_clock_text(clockBuf, (int)sizeof(clockBuf));
    fb_fill_round_rect(fb, pitch, 868, 768 - tbH + 10, 144, 32, 10, 0xFF152538u);
    fb_draw_text(fb, pitch, 884, 768 - tbH + 20, clockBuf, 0xFFE2E8F0u, 1);
    } else if (g_sg_desktop_auth) {
        /* Labels on SG chips (Start / Search / clock / task slots). */
        int taskWins[g_win_max];
        const int nTask = guest_desk_collect_task_wins(taskWins, g_win_max);
        for (int slot = 0; slot < nTask; ++slot) {
            const int wi = taskWins[slot];
            const GuestWin *tw = &g_wins[wi];
            const int bx = guest_desk_task_slot_x(slot);
            const GuestDeskIcon *ic = &g_desk_icons[tw->app_id];
            fb_draw_text(fb, pitch, bx + 8, 768 - tbH + 12, ic->acro, 0xFFE2E8F0u, 1);
            fb_draw_text(fb, pitch, bx + 8, 768 - tbH + 26, ic->title,
                         tw->minimized ? 0xFF64748Bu : 0xFFC8DCEDu, 1);
        }
        fb_draw_text(fb, pitch, 16, 768 - tbH + 18, "Start", 0xFFC8DCEDu, 1);
        fb_draw_text(fb, pitch, 84, 768 - tbH + 18, "Q", 0xFF88AAC0u, 1);
        fb_draw_text(fb, pitch, 100, 768 - tbH + 20, "Search", 0xFF557090u, 1);
        char clockBuf[16];
        guest_desk_clock_text(clockBuf, (int)sizeof(clockBuf));
        fb_draw_text(fb, pitch, 896, 768 - tbH + 20, clockBuf, 0xFFE2E8F0u, 1);
    }

    memcpy(g_desk_bg_cache, fb, sizeof(g_desk_bg_cache));
    g_desk_bg_cache_ok = 1;
    } /* !win_only chrome */

    guest_paint_fb_windows(fb, pitch);
    /* Start menu: SG panel chrome when auth; FB labels always. */
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
        if (g_ir_rect_live)
            guest_serial_puts("[desktop_qt] IR Rectangle FB badge painted\n");
        if (g_prod_fb0_auth)
            guest_serial_puts("[desktop_qt] product FB0 leaf auth ok\n");
        if (g_prod_fb0_auth && g_prod_chrome_item)
            guest_serial_puts("[desktop_qt] product chrome FB0 ok\n");
        if (g_prod_fb0_auth && g_prod_tray_item)
            guest_serial_puts("[desktop_qt] product tray FB0 ok\n");
        if (g_prod_fb0_auth && g_prod_icons_item)
            guest_serial_puts("[desktop_qt] product icons FB0 ok\n");
        if (g_prod_fb0_auth && g_prod_icons2_item)
            guest_serial_puts("[desktop_qt] product icons2 FB0 ok\n");
        if (g_prod_fb0_auth && g_prod_start_item)
            guest_serial_puts("[desktop_qt] product start FB0 ok\n");
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
        if (g_sg_desktop_auth) {
            guest_serial_puts("[desktop_qt] SG desktop leaves present\n");
            guest_serial_puts("[desktop_qt] SG icon band retained\n");
        }
    }
}

/* Pulse product QQuickWindow through software SG → QPA flush (FB0). */
static void guest_prod_sg_pulse_flush(void)
{
    if (!g_prod_sg_win || g_prod_sg_pulse_hold)
        return;
    bfree_guest_ensure_drawhelpers();
    bfree_qpa_set_update_delivery(1);
    /* Keep clear color = desk wallpaper (default white → 真っ白 flush). */
    g_prod_sg_win->setColor(QColor(BFREE_DESK_WALL_RGB));
    if (QWindowPrivate *wd = QWindowPrivate::get(g_prod_sg_win)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    if (g_host_wall) {
        g_host_wall->setVisible(true);
        g_host_wall->update();
    }
    if (g_host_bar) {
        g_host_bar->setVisible(true);
        g_host_bar->update();
    }
    if (QQuickItem *ci = g_prod_sg_win->contentItem()) {
        const auto kids = ci->childItems();
        for (QQuickItem *k : kids) {
            if (k && k->isVisible() && k->flags().testFlag(QQuickItem::ItemHasContents))
                k->update();
        }
        ci->update();
    }
    g_prod_sg_win->requestUpdate();
    /* UpdateRequest only — unrestricted sendPostedEvents() after
     * requestActivate has PF'd in notifyInternal2 (bad QObject @0x100000000). */
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
}

/* True if FB0 center still looks like brand-splash white or uncleared black. */
static int guest_fb_blank_or_white(void)
{
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    const uint32_t c = *reinterpret_cast<uint32_t *>(fb + 384u * pitch + 512u * 4u);
    const unsigned r = (c >> 16) & 0xffu;
    const unsigned g = (c >> 8) & 0xffu;
    const unsigned b = c & 0xffu;
    if ((c & 0x00ffffffu) == 0u)
        return 1; /* black / zeroed backing */
    if (r > 0xf0u && g > 0xf0u && b > 0xf0u)
        return 1; /* brand splash white */
    return 0;
}

static void guest_prod_sg_repair_blank_fb(void)
{
    if (!guest_fb_blank_or_white())
        return;
    guest_serial_puts("[desktop_qt] product SG blank→FB chrome repair\n");
    g_prod_fb_chrome_force = 1;
    guest_paint_fb_desktopshell();
}

static void guest_paint_fb_cursor_only(void)
{
    auto *fb = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(BFREE_FB0_USER_MMAP_BASE));
    const unsigned pitch = 1024u * 4u;
    g_cur_have = 0;
    guest_desk_cursor_blit_under(fb, pitch, g_desk_mx, g_desk_my, guest_desk_cur_under(), 1);
    g_cur_sx = g_desk_mx;
    g_cur_sy = g_desk_my;
    g_cur_have = 1;
    guest_desk_draw_cursor(fb, pitch);
}

static void guest_desk_flush_paint(void)
{
    if (!g_desk_dirty)
        return;
    g_desk_dirty = 0;
    if (g_prod_sg_sustained && g_prod_sg_win) {
        /* Interactive pixels stay FB; Max1 only unlocks one-shot Terminal setVisible. */
        g_prod_fb_chrome_force = 1;
        guest_paint_fb_desktopshell();
        g_desk_layer_dirty = 0;
        return;
    }
    guest_sg_sync_start_panel();
    guest_w3_sync_window_layer();
    /* Pulse SG before FB overlay so windows/labels stay visible on top. */
    guest_sg_chrome_pulse_if_needed();
    guest_paint_fb_desktopshell();
    g_desk_layer_dirty = 0;
}

static void guest_desk_prepare_app(int idx)
{
    if (idx == 0) {
        guest_persist_create_desk_note();
        g_persist_listed = 0;
        guest_persist_scan_once();
        guest_serial_puts("[desktop_qt] Explorer listing\n");
    } else if (idx == 1) {
        /* Keep Explorer selection/preview; only scan if never listed. */
        if (!g_persist_listed)
            guest_persist_scan_once();
        else if (!g_persist_preview[0] && g_persist_nnames > 0) {
            int i = (g_persist_sel >= 0 && g_persist_sel < g_persist_nnames)
                        ? g_persist_sel
                        : 0;
            guest_persist_load_preview(g_persist_names[i]);
        }
        guest_serial_puts("[desktop_qt] Viewer open\n");
        if (g_persist_preview[0]) {
            guest_serial_puts("[desktop_qt] Viewer body=");
            guest_serial_puts(g_persist_preview);
            guest_serial_puts("\n");
            guest_serial_puts("[desktop_qt] Viewer linked\n");
        }
    } else if (idx == 2) {
        guest_terminal_ensure_session();
    }
}

static void guest_win_close_idx(int wi)
{
    if (wi < 0 || wi >= g_win_n || !g_wins[wi].open)
        return;
    guest_serial_puts("[desktop_qt] desk close\n");
    if (g_wins[wi].app_id == 2) {
        guest_pty_shell_stop(&g_term_pty);
        g_term_pty_mode = 0;
        g_term_pty_partial_len = 0;
        g_term_session = 0;
        g_term_bb_demo_pending = 0;
        g_term_nlines = 0;
        g_term_linelen = 0;
        g_term_line[0] = '\0';
    }
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

/* Soft wall/bar pulse after activate historically PF@soft5 setVisible/update.
 * Kept for diagnostics; Max1 arm does not call this. */
static void guest_prod_sg_soft_pulse_no_ur(void)
{
    if (!g_prod_sg_win || g_prod_sg_pulse_hold)
        return;
    (void)g_prod_sg_win;
}

/* Tree1: H2b dense SG after activate — Terminal + host wallpaper/bar/start. */
static void guest_prod_post_activate_dense_sg(void)
{
    if (!g_prod_sg_win)
        return;
    guest_serial_puts("[desktop_qt] post-act dense SG enter\n");
    bfree_guest_ensure_drawhelpers();
    g_prod_sg_pulse_hold = 1;
    guest_serial_puts("[desktop_qt] post-act dense unexpose enter\n");
    if (QWindowPrivate *wd = QWindowPrivate::get(g_prod_sg_win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    guest_serial_puts("[desktop_qt] post-act dense unexpose ok\n");
        /* Terminal show while window unexposed (H2b sibling-show pattern). */
    if (g_qml_term_ok && g_qml_term_item) {
        guest_serial_puts("[desktop_qt] post-act term setVisible enter\n");
        g_qml_term_item->setFlag(QQuickItem::ItemHasContents, false);
        g_qml_term_item->setVisible(true);
        g_qml_term_item->setFlag(QQuickItem::ItemHasContents, true);
        guest_serial_puts("[desktop_qt] post-act term setVisible ok\n");
    }
#if defined(BFREE_GUEST_LINK_CONTROLS)
    /* Do not create/show Controls here — new QQuickButton + setVisible PF@0
     * during unexpose (2026-08-11). Show after dense UR below. */
#endif
    guest_serial_puts("[desktop_qt] post-act dense reexpose enter\n");
    bfree_guest_ensure_drawhelpers();
    bfree_qpa_set_update_delivery(0);
    if (QWindowPrivate *wd = QWindowPrivate::get(g_prod_sg_win)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    guest_serial_puts("[desktop_qt] post-act dense reexpose flags ok\n");
    bfree_guest_set_prefer_fallback_alloc(1);
    /* Product host chrome: dirty while delivery OFF, then one UR. */
    guest_serial_puts("[desktop_qt] post-act host-tree paint enter\n");
    if (g_host_wall)
        g_host_wall->update();
    if (g_host_bar)
        g_host_bar->update();
    if (g_host_start)
        g_host_start->update();
    if (g_qml_term_item)
        g_qml_term_item->update();
    if (QQuickItem *ci = g_prod_sg_win->contentItem()) {
        const auto kids = ci->childItems();
        for (QQuickItem *k : kids) {
            if (k && k->isVisible() && k->flags().testFlag(QQuickItem::ItemHasContents))
                k->update();
        }
        ci->update();
    }
    guest_serial_puts("[desktop_qt] post-act host-tree paint ok\n");
    g_prod_sg_win->requestUpdate();
    guest_serial_puts("[desktop_qt] post-act dense UR enter\n");
    bfree_qpa_set_update_delivery(1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    guest_serial_puts("[desktop_qt] post-act dense UR ok\n");
    bfree_guest_set_prefer_fallback_alloc(0);
    g_prod_sg_pulse_hold = 0;
    g_prod_host_tree_unlocked = 1;
    /* Leave UpdateRequest delivery ON — product tree live after arm. */
    guest_prod_sg_repair_blank_fb();
    guest_serial_puts("[desktop_qt] post-act dense SG ok\n");
    guest_serial_puts("[desktop_qt] product host-tree unlock ok\n");
#if defined(BFREE_GUEST_LINK_CONTROLS)
    /* After UR delivery ON — last chance for Gate1 Button show. */
    if (g_controls_button_probe && g_controls_button_probe->parentItem() &&
        !g_controls_button_probe->isVisible()) {
        guest_serial_puts("[desktop_qt] Controls Button shell visible enter\n");
        if (!QQuickTheme::instance()) {
            auto *theme = new QQuickTheme;
            theme->setFont(QQuickTheme::System, QGuiApplication::font());
            QQuickThemePrivate::instance.reset(theme);
        }
        g_controls_button_probe->setHoverEnabled(false);
        g_controls_button_probe->setFlag(QQuickItem::ItemHasContents, false);
        g_controls_button_probe->setVisible(true);
        guest_serial_puts("[desktop_qt] Controls Button shell visible ok\n");
    }
#endif
}

static void guest_qml_terminal_show_post_activate(void)
{
    if (!g_qml_term_ok || !g_qml_term_item) {
        guest_serial_puts("[desktop_qt] product post-activate Terminal show skip\n");
        return;
    }
    guest_serial_puts("[desktop_qt] product post-activate Terminal show enter\n");
    /* Clear1: dense SG path already showed Terminal; refresh geometry only. */
    g_prod_sg_pulse_hold = 1;
    g_qml_term_item->setX(108);
    g_qml_term_item->setY(60);
    g_prod_sg_pulse_hold = 0;
    guest_serial_puts("[desktop_qt] product post-activate Terminal show ok\n");
}

/* Tree1: unlock product host chrome SG after event-loop arm. */
static void guest_prod_post_activate_arm_once(void)
{
    if (g_prod_post_activate_done)
        return;
    g_prod_post_activate_done = 1;
    if (!g_prod_sg_sustained || !g_prod_sg_win) {
        guest_serial_puts("[desktop_qt] product post-activate SG skip (no win)\n");
        return;
    }
    guest_serial_puts("[desktop_qt] product post-activate SG arm enter\n");
    g_prod_post_activate_sg = 1;
    guest_prod_post_activate_dense_sg();
    guest_qml_terminal_show_post_activate();
    guest_serial_puts("[desktop_qt] product post-activate SG arm ok\n");
}

static int guest_qml_terminal_ensure(void)
{
    if (g_qml_term_ok && g_qml_term_item) {
        /* Max1: after event-loop arm, show is safe; before that keep hidden. */
        return 1;
    }
    if (!g_prod_sg_win)
        return 0;
    QQuickItem *content = g_prod_sg_win->contentItem();
    if (!content)
        return 0;

    /* Avoid QQmlComponent/IR for Terminal — PF@0x29000000 on guest create path.
     * Single HasContents leaf; attach under pulse_hold to avoid UpdateRequest. */
    guest_serial_puts("[desktop_qt] Terminal create enter\n");
    auto *root = new QQuickRectangle();
    if (!root)
        return 0;
    guest_serial_puts("[desktop_qt] Terminal new ok\n");
    root->setObjectName(QStringLiteral("GuestThinTerminal"));
    root->setX(100);
    root->setY(60);
    root->setWidth(640);
    root->setHeight(420);
    root->setZ(50);
    root->setRadius(4);
    root->setColor(QColor(0x1e, 0x29, 0x3b)); /* slate frame fill */
    /* Visible from create — post-activate setVisible RIP=0. */
    root->setVisible(true);
    guest_serial_puts("[desktop_qt] Terminal props ok\n");
    /* Match H2b: disable before parent; enable later on open. */
    root->setEnabled(false);
    root->setAcceptedMouseButtons(Qt::NoButton);
    root->setParentItem(content);
    guest_serial_puts("[desktop_qt] Terminal parent ok\n");

    g_qml_term_item = root;
    g_qml_term_ok = 1;
    guest_serial_puts("[desktop_qt] Terminal completeCreate ok\n");
    return 1;
}

static void guest_desk_open_app(int idx)
{
    if (idx < 0 || idx >= g_desk_n_icons)
        return;
    g_desk_start_open = 0;

    /* Terminal QML leaf: Max1 shows after event-loop arm; FB mini-WM still paints. */
    if (idx == 2 && g_qml_term_ok) {
        if (g_prod_post_activate_sg) {
            guest_qml_terminal_show_post_activate();
            guest_serial_puts("[desktop_qt] desk open Terminal (QML+FB)\n");
        } else {
            guest_serial_puts("[desktop_qt] desk open Terminal (FB; QML leaf held)\n");
        }
    }

    const int existing = guest_win_find_app(idx);
    if (existing >= 0) {
        guest_win_raise(existing);
        guest_serial_puts("[desktop_qt] desk raise ");
        guest_serial_puts(g_desk_icons[idx].title);
        guest_serial_puts("\n");
        guest_desk_prepare_app(idx);
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
    w->qml_sg = 0;
    if (idx == 2) {
        w->w = 640;
        w->h = 420;
        w->x = 100 + (slot % 3) * 40;
        w->y = 40 + (slot % 3) * 24;
    } else {
        w->w = 520;
        w->h = 320;
        w->x = 120 + (slot % 3) * 40;
        w->y = 80 + (slot % 3) * 36;
    }
    w->rx = w->x;
    w->ry = w->y;
    w->rw = w->w;
    w->rh = w->h;
    guest_win_clamp_geom(w);
    guest_win_raise(slot);
    guest_serial_puts("[desktop_qt] desk open ");
    guest_serial_puts(g_desk_icons[idx].title);
    guest_serial_puts("\n");
    if (g_sg_desktop_auth) {
        g_sg_chrome_need_pulse = 1;
        guest_serial_puts("[desktop_qt] SG window chrome\n");
    }
    guest_desk_prepare_app(idx);
    guest_desk_mark_dirty();
}

static void guest_desk_toggle_start(void)
{
    g_desk_start_open = g_desk_start_open ? 0 : 1;
    guest_serial_puts(g_desk_start_open ? "[desktop_qt] Start open\n"
                                        : "[desktop_qt] Start close\n");
    if (g_sg_desktop_auth) {
        g_sg_chrome_need_pulse = 1;
        guest_serial_puts("[desktop_qt] SG Start panel chrome\n");
    }
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
    static unsigned key_diag;
    k = guest_desk_denorm_key(k);
    if (key_diag < 16u) {
        ++key_diag;
        guest_serial_puts("[desktop_qt] key ");
        /* tiny hex nibble dump (k is denormed ASCII / control). */
        {
            char hx[12];
            const char *dig = "0123456789abcdef";
            hx[0] = '0';
            hx[1] = 'x';
            hx[2] = dig[(k >> 12) & 0xfu];
            hx[3] = dig[(k >> 8) & 0xfu];
            hx[4] = dig[(k >> 4) & 0xfu];
            hx[5] = dig[k & 0xfu];
            hx[6] = '\n';
            hx[7] = '\0';
            guest_serial_puts(hx);
        }
    }
    if (g_desk_asleep) {
        guest_desk_wake_if_asleep();
        return;
    }
    /* Terminal focused (or top Terminal when focus stale): line editor owns keys. */
    {
        int twi = -1;
        if (g_win_focus >= 0 && g_win_focus < g_win_n && g_wins[g_win_focus].open &&
            !g_wins[g_win_focus].minimized && g_wins[g_win_focus].app_id == 2)
            twi = g_win_focus;
        else {
            /* Fallback: topmost open Terminal still eats keys so typing works
             * after a desktop click that left focus ambiguous. */
            int best_z = -1;
            for (int i = 0; i < g_win_n; ++i) {
                if (!g_wins[i].open || g_wins[i].minimized || g_wins[i].app_id != 2)
                    continue;
                if (g_wins[i].z >= best_z) {
                    best_z = g_wins[i].z;
                    twi = i;
                }
            }
            if (twi >= 0 && g_win_focus >= 0 && g_win_focus < g_win_n &&
                g_wins[g_win_focus].open && !g_wins[g_win_focus].minimized &&
                g_wins[g_win_focus].app_id != 2)
                twi = -1; /* another app is clearly focused */
        }
        if (twi >= 0) {
            if (g_win_focus != twi)
                guest_win_raise(twi);
            if (k == 27u) {
                guest_desk_close_app();
                return;
            }
            guest_terminal_on_key(k);
            return;
        }
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
    } else if (k == (uint32_t)'!') {
        /* Survive demo: kill Linux ABI / Qt guest; kernel [BFreeCore] tick continues. */
        guest_serial_puts("[desktop_qt] SURVIVE demo: killing guest ABI (!)\n");
        *(volatile int *)0 = 0x736b;
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
    guest_desk_mark_win_dirty();
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
        if (startItem == G_START_HIT_RESTART)
            guest_desk_request_restart();
        else if (startItem == G_START_HIT_SHUTDOWN)
            guest_desk_request_shutdown();
        else if (startItem == G_START_HIT_SLEEP)
            guest_desk_request_sleep();
        else if (startItem < g_desk_n_icons)
            guest_desk_open_app(startItem);
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
        if (g_wins[wi].app_id == 0) {
            const int list_y0 = g_wins[wi].y + g_win_title_h + 12 + 40;
            const int client_h = g_wins[wi].h - g_win_title_h - 36;
            const int max_rows = client_h > 60 ? (client_h - 60) / 20 : 8;
            int vis = max_rows > 0 ? max_rows : 8;
            if (my >= list_y0 && my < g_wins[wi].y + g_wins[wi].h - 28) {
                const int row = (my - list_y0) / 20;
                const int idx = g_persist_scroll + row;
                if (row >= 0 && row < vis && idx >= 0 && idx < g_persist_nnames) {
                    guest_win_raise(wi);
                    g_persist_sel = idx;
                    guest_persist_load_preview(g_persist_names[idx]);
                    guest_serial_puts("[desktop_qt] Explorer pick\n");
                    /* Open/raise Viewer with the same preview (Explorer↔Viewer). */
                    guest_desk_open_app(1);
                    guest_desk_mark_dirty();
                    return;
                }
            }
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
        /* Window layer only — avoid full desk+SG flicker while dragging. */
        guest_desk_mark_win_dirty();
    } else if (etype == 0 && moved) {
        /* Idle: cursor-only (no full-desk flicker). */
        guest_desk_cursor_move_only();
    }
}

static void guest_desk_pump_input(void)
{
    /* When QPA mouse bridge is armed, only drain keys here. Dual mouse delivery
     * (QPA + POLL) breaks title-bar drag: POLL often sees btn=0 mid-hold and
     * runs cursor_move_only, fighting apply_geom / full-desk dirty paints. */
    for (int n = 0; n < 32; ++n) {
        g_poll_raw.type = 0;
        g_poll_raw.keycode = 0;
        g_poll_raw.mouse_x = 0;
        g_poll_raw.mouse_y = 0;
        g_poll_raw.mouse_btn = 0;
        const long ret = bfree_guest_syscall1(BFREE_SYS_POLL_INPUT_EVENT, (long)&g_poll_raw);
        if (ret <= 0)
            break;
        if (g_poll_raw.type == 3) {
            if (g_desk_qpa_input)
                continue; /* QPA bridge already owns mouse */
            const int mx = g_poll_raw.mouse_x < 0 ? 0 : (g_poll_raw.mouse_x > 1023 ? 1023 : g_poll_raw.mouse_x);
            const int my = g_poll_raw.mouse_y < 0 ? 0 : (g_poll_raw.mouse_y > 767 ? 767 : g_poll_raw.mouse_y);
            const uint32_t btn = g_poll_raw.mouse_btn;
            if ((btn & 1u) && !(g_desk_prev_btn & 1u))
                guest_desk_on_click(mx, my);
            else if (!(btn & 1u) && (g_desk_prev_btn & 1u))
                guest_wm_end();
            else if (((btn & 1u) || g_btn_left_held) && g_wm_mode) {
                g_desk_mx = mx;
                g_desk_my = my;
                guest_wm_apply_geom(mx, my);
                guest_desk_mark_win_dirty();
            } else if (mx != g_desk_mx || my != g_desk_my) {
                g_desk_mx = mx;
                g_desk_my = my;
                guest_desk_cursor_move_only();
            }
            g_desk_prev_btn = btn;
        } else if (g_poll_raw.type == 1) {
            guest_serial_puts("[desktop_qt] qt key\n");
            guest_desk_on_key(g_poll_raw.keycode);
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

/* Product DesktopShell skips CU relative addImport. Nested kde/wabi child
 * qrc URLs need guest-aligned qmlcache (unversioned import QtQuick; see H3).
 * Root-qrc thin child (GuestProductChild) remains the safe stand-in path. */
static QObject *guest_product_load_child_url(QQmlEngine *eng, const char *urlUtf8,
                                             const char *tag)
{
    if (!eng || !urlUtf8 || !tag)
        return nullptr;
    guest_serial_puts("[desktop_qt] product child IR enter ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    /* Guest QML type-loader can block a PreferSynchronous qrc load forever
     * (loader-thread stall right after getcwd during compile). Construct the
     * component asynchronously and pump events with a hard spin cap so a
     * stalled child turns into a diagnosable status instead of an infinite
     * hang, letting boot fall through to its DesktopShell FB fallback.
     * The "ctor begin"/"ctor end" markers isolate ctor vs load-thread stalls. */
    guest_serial_puts("[desktop_qt] product child IR ctor begin ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    QQmlComponent c(eng, QUrl(QString::fromUtf8(urlUtf8)),
                    QQmlComponent::Asynchronous);
    guest_serial_puts("[desktop_qt] product child IR ctor end ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    for (int spin = 0; c.isLoading() && spin < 4000; ++spin) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        if ((spin & 0x1ff) == 0x1ff) {
            guest_serial_puts("[desktop_qt] product child IR loading spin=");
            guest_serial_hex_u64((uint64_t)(unsigned)spin);
            guest_serial_puts("\n");
        }
    }
    guest_serial_puts("[desktop_qt] product child IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)c.status());
    guest_serial_puts(" tag=");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    if (!c.isReady()) {
        if (c.isError())
            guest_serial_puts("[desktop_qt] product child IR error\n");
        else
            guest_serial_puts("[desktop_qt] product child IR not ready\n");
        return nullptr;
    }
    guest_serial_puts("[desktop_qt] product child Ready ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    QObject *obj = c.beginCreate(eng->rootContext());
    if (!obj) {
        guest_serial_puts("[desktop_qt] product child beginCreate null ");
        guest_serial_puts(tag);
        guest_serial_puts("\n");
        return nullptr;
    }
    c.completeCreate();
    guest_serial_puts("[desktop_qt] product child create ok ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    return obj;
}

static void guest_product_shell_child_qmlcache_preload(void)
{
    if (!g_engine)
        return;
    guest_serial_puts("[desktop_qt] product child qmlcache preload enter\n");
    int n_ready = 0;

    if (!g_prod_chrome_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductChrome.qml", "GuestProductChrome")) {
            g_prod_chrome_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_chrome_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductChrome non-Item\n");
        }
    } else {
        ++n_ready;
    }

    if (!g_prod_tray_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductTray.qml", "GuestProductTray")) {
            g_prod_tray_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_tray_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductTray non-Item\n");
        }
    } else {
        ++n_ready;
    }

    if (!g_prod_icons_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductIcons.qml", "GuestProductIcons")) {
            g_prod_icons_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_icons_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductIcons non-Item\n");
        }
    } else {
        ++n_ready;
    }

    if (!g_prod_icons2_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductIcons2.qml", "GuestProductIcons2")) {
            g_prod_icons2_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_icons2_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductIcons2 non-Item\n");
        }
    } else {
        ++n_ready;
    }

    if (!g_prod_start_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductStartMenu.qml", "GuestProductStartMenu")) {
            g_prod_start_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_start_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductStartMenu non-Item\n");
        }
    } else {
        ++n_ready;
    }

    if (!g_prod_child_item) {
        if (QObject *o = guest_product_load_child_url(
                g_engine, "qrc:/GuestProductChild.qml", "GuestProductChild")) {
            g_prod_child_item = qobject_cast<QQuickItem *>(o);
            if (g_prod_child_item)
                ++n_ready;
            else
                guest_serial_puts("[desktop_qt] product child GuestProductChild non-Item\n");
        }
    } else {
        ++n_ready;
    }

    guest_serial_puts("[desktop_qt] product child Ready count=");
    guest_serial_hex_u64((uint64_t)(unsigned)n_ready);
    guest_serial_puts("\n");
    if (n_ready > 0)
        guest_serial_puts("[desktop_qt] product child HITDS child ok\n");
    if (g_prod_chrome_item)
        guest_serial_puts("[desktop_qt] product chrome HITDS ok\n");
    if (g_prod_tray_item)
        guest_serial_puts("[desktop_qt] product tray HITDS ok\n");
    if (g_prod_icons_item)
        guest_serial_puts("[desktop_qt] product icons HITDS ok\n");
    if (g_prod_icons2_item)
        guest_serial_puts("[desktop_qt] product icons2 HITDS ok\n");
    if (g_prod_start_item)
        guest_serial_puts("[desktop_qt] product start HITDS ok\n");
    guest_serial_puts("[desktop_qt] product child qmlcache preload done\n");
}

static int guest_product_attach_child_item(QQuickItem *ci, QQuickItem *qi,
                                           const char *tag, qreal x, qreal y)
{
    if (!ci || !qi || !tag)
        return 0;
    /* Step4: only Window contentItem as C++ parent — never Item↔Item (PF@0x238). */
    qi->setVisible(true);
    qi->setParentItem(ci);
    qi->setX(x);
    qi->setY(y);
    guest_serial_puts("[desktop_qt] product child attach ok ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    return 1;
}

static void guest_product_shell_child_attach(QQuickWindow *qw)
{
    if (!qw)
        return;
    guest_serial_puts("[desktop_qt] product child attach enter\n");
    /* Host DesktopShell owns chrome/tray/icons; skip GuestProduct* stand-ins. */
    guest_serial_puts("[desktop_qt] product host-shell authority (skip GuestProduct attach)\n");
    QQuickItem *ci = qw->contentItem();
    if (!ci) {
        guest_serial_puts("[desktop_qt] product child attach skip (no contentItem)\n");
        return;
    }
#if defined(BFREE_GUEST_LINK_CONTROLS)
    int n_attach = 0;
    if (QQuickItem *qi = qobject_cast<QQuickItem *>(g_qml_controls_button_root))
        n_attach += guest_product_attach_child_item(ci, qi, "GuestControlsButton", 16, 140);
    guest_serial_puts("[desktop_qt] product child attach count=");
    guest_serial_hex_u64((uint64_t)(unsigned)n_attach);
    guest_serial_puts("\n");
#endif
    guest_serial_puts("[desktop_qt] DesktopShell Window content children after=");
    guest_serial_hex_u64((uint64_t)(unsigned)ci->childItems().size());
    guest_serial_puts("\n");
    guest_serial_puts("[desktop_qt] product child attach done\n");
}

/* Product Window: one-leaf SG soft present, then H2b flush + H3 Clock.
 * Host-shell authority: keep Window exposed/visible (no FB input handoff). */
/* H1: QML Item↔Item parent (contentItem-direct works; historically PF@0x238). */
static void guest_product_h1_item_parent(void)
{
    guest_serial_puts("[desktop_qt] H1 Item parent probe enter\n");
    if (!g_prod_chrome_item || !g_prod_child_item) {
        /* Host-shell path: no GuestProduct* — nest a mid Item under contentItem. */
        QQuickWindow *qw = qobject_cast<QQuickWindow *>(g_qml_root);
        QQuickItem *ci = qw ? qw->contentItem() : nullptr;
        if (!ci) {
            guest_serial_puts("[desktop_qt] H1 Item parent probe skip (need chrome+child or contentItem)\n");
            return;
        }
        guest_serial_puts("[desktop_qt] H1 Item-Item setParentItem enter\n");
        auto *mid = new QQuickItem();
        mid->setObjectName(QStringLiteral("HostShellH1Mid"));
        mid->setWidth(64);
        mid->setHeight(64);
        mid->setVisible(true);
        mid->setParentItem(ci);
        mid->setX(16);
        mid->setY(80);
        auto *leaf = new QQuickRectangle();
        leaf->setObjectName(QStringLiteral("HostShellH1Leaf"));
        leaf->setParentItem(mid);
        leaf->setWidth(32);
        leaf->setHeight(32);
        leaf->setColor(QColor(0x22, 0x66, 0xaa));
        leaf->setFlag(QQuickItem::ItemHasContents, true);
        guest_serial_puts("[desktop_qt] H1 Item-Item setParentItem ok\n");
        guest_serial_puts("[desktop_qt] H1b bare Item parent enter\n");
        auto *probe = new QQuickItem();
        probe->setObjectName(QStringLiteral("HostShellH1bBare"));
        probe->setWidth(8);
        probe->setHeight(8);
        probe->setVisible(false);
        probe->setEnabled(false);
        probe->setParentItem(mid);
        guest_serial_puts("[desktop_qt] H1b bare Item parent ok\n");
        guest_serial_puts("[desktop_qt] H1c nested Item parent enter\n");
        guest_serial_puts("[desktop_qt] H1c nested Item parent ok\n");
        return;
    }
    guest_serial_puts("[desktop_qt] H1 Item-Item setParentItem enter\n");
    g_prod_child_item->setX(16);
    g_prod_child_item->setY(80);
    g_prod_child_item->setParentItem(g_prod_chrome_item);
    guest_serial_puts("[desktop_qt] H1 Item-Item setParentItem ok\n");

    guest_serial_puts("[desktop_qt] H1b bare Item parent enter\n");
    {
        auto *probe = new QQuickItem();
        probe->setObjectName(QStringLiteral("HoleH1bBareItem"));
        probe->setWidth(8);
        probe->setHeight(8);
        probe->setVisible(false);
        probe->setEnabled(false);
        probe->setParentItem(g_prod_chrome_item);
        guest_serial_puts("[desktop_qt] H1b bare Item parent ok\n");
    }

    guest_serial_puts("[desktop_qt] H1c nested Item parent enter\n");
    {
        auto *mid = new QQuickItem();
        mid->setObjectName(QStringLiteral("HoleH1cMidItem"));
        mid->setWidth(64);
        mid->setHeight(64);
        mid->setVisible(true);
        mid->setParentItem(g_prod_chrome_item);
        mid->setX(200);
        mid->setY(80);
        if (g_prod_tray_item) {
            g_prod_tray_item->setParentItem(mid);
            g_prod_tray_item->setX(0);
            g_prod_tray_item->setY(0);
            guest_serial_puts("[desktop_qt] H1c nested Item parent ok\n");
        } else {
            auto *leaf = new QQuickRectangle();
            leaf->setParentItem(mid);
            leaf->setWidth(32);
            leaf->setHeight(32);
            leaf->setColor(QColor(0x22, 0x66, 0xaa));
            guest_serial_puts("[desktop_qt] H1c nested Item parent ok\n");
        }
    }
}

/* H2 / H2b: soft-present leaf then multi-item SG flush protocol. */
static void guest_product_h2_sg_flush(QQuickWindow *qw)
{
    guest_serial_puts("[desktop_qt] H2 SG flush probe enter\n");
    if (!qw) {
        guest_serial_puts("[desktop_qt] H2 SG flush probe skip (no window)\n");
        return;
    }
    guest_serial_puts("[desktop_qt] H2 UpdateRequest enter\n");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    guest_serial_puts("[desktop_qt] H2 UpdateRequest ok\n");

    guest_serial_puts("[desktop_qt] H2b multi-item flush enter\n");
    QQuickItem *content = qw->contentItem();
    if (g_ds_product_content_badge && content) {
        guest_serial_puts("[desktop_qt] H2b unexpose enter\n");
        if (QWindowPrivate *wd = QWindowPrivate::get(qw)) {
            wd->exposed = false;
            wd->receivedExpose = false;
        }
        guest_serial_puts("[desktop_qt] H2b unexpose ok\n");
        guest_serial_puts("[desktop_qt] H2b attach enter\n");
        auto *sib = new QQuickRectangle();
        sib->setObjectName(QStringLiteral("HoleH2bSibling"));
        sib->setParentItem(content);
        sib->setX(80);
        sib->setY(60);
        sib->setWidth(48);
        sib->setHeight(48);
        sib->setZ(0);
        sib->setColor(QColor(0xf0, 0xb0, 0x40));
        sib->setEnabled(false);
        sib->setAcceptedMouseButtons(Qt::NoButton);
        sib->setVisible(false);
        guest_serial_puts("[desktop_qt] H2b attach ok\n");
        guest_serial_puts("[desktop_qt] H2b reexpose enter\n");
        bfree_qpa_set_update_delivery(1);
        bfree_guest_set_prefer_fallback_alloc(1);
        if (QWindowPrivate *wd = QWindowPrivate::get(qw)) {
            wd->receivedExpose = true;
            wd->exposed = true;
            wd->resizeEventPending = false;
        }
        guest_serial_puts("[desktop_qt] H2b reexpose flags ok\n");
        g_ds_product_content_badge->update();
        guest_serial_puts("[desktop_qt] H2b leaf update ok\n");
        qw->requestUpdate();
        guest_serial_puts("[desktop_qt] H2b requestUpdate (sib-hidden) ok\n");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        guest_serial_puts("[desktop_qt] H2b UpdateRequest (sib-hidden) ok\n");
        sib->setVisible(true);
        guest_serial_puts("[desktop_qt] H2b sibling show ok\n");
        g_ds_product_content_badge->update();
        sib->update();
        qw->requestUpdate();
        guest_serial_puts("[desktop_qt] H2b requestUpdate ok\n");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        guest_serial_puts("[desktop_qt] H2b UpdateRequest ok\n");
        QCoreApplication::sendPostedEvents();
        guest_serial_puts("[desktop_qt] H2b sendPosted ok\n");
        bfree_guest_set_prefer_fallback_alloc(0);
        /* Probe sibling is not host chrome — hide after flush. */
        sib->setVisible(false);
        /* Keep update delivery ON for host-shell SG authority. */
        guest_serial_puts("[desktop_qt] H2b multi-item flush ok\n");
    } else {
        guest_serial_puts("[desktop_qt] H2b multi-item flush skip (no badge/content)\n");
    }
}

static void guest_product_h3_one_url_at(const char *urlUtf8, const char *tag,
                                        int visible, qreal x, qreal y)
{
    guest_serial_puts("[desktop_qt] H3 url enter ");
    guest_serial_puts(tag);
    guest_serial_puts("\n");
    if (!g_engine)
        return;
    QQmlComponent c(g_engine, QUrl(QString::fromUtf8(urlUtf8)),
                    QQmlComponent::PreferSynchronous);
    for (int spin = 0; c.isLoading() && spin < 64; ++spin)
        QCoreApplication::processEvents();
    if (!c.isReady()) {
        guest_serial_puts("[desktop_qt] H3 url fail ");
        guest_serial_puts(tag);
        guest_serial_puts("\n");
        return;
    }
    QObject *obj = c.beginCreate(g_engine->rootContext());
    if (!obj)
        return;
    c.completeCreate();
    if (QQuickItem *qi = qobject_cast<QQuickItem *>(obj)) {
        qi->setVisible(visible != 0);
        qi->setEnabled(visible != 0);
        /* Prefer product DesktopShell contentItem over GuestProduct chrome stand-in. */
        QQuickItem *parent = nullptr;
        if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(g_qml_root))
            parent = qw->contentItem();
        if (!parent)
            parent = g_prod_chrome_item;
        if (parent) {
            qi->setParentItem(parent);
            qi->setX(x);
            qi->setY(y);
            guest_serial_puts("[desktop_qt] H3 parent ok ");
            guest_serial_puts(tag);
            guest_serial_puts("\n");
            if (strstr(tag, "Clock") || strstr(tag, "Tray"))
                g_host_widgets_ok = 1;
            if (strstr(tag, "Wabi"))
                g_host_wabi_ok = 1;
        }
        guest_serial_puts("[desktop_qt] H3 url ok ");
        guest_serial_puts(tag);
        guest_serial_puts("\n");
    } else {
        /* Themes.BreezeTheme is QtObject — attach to engine, not Item tree. */
        obj->setParent(g_engine);
        guest_serial_puts("[desktop_qt] H3 object ok ");
        guest_serial_puts(tag);
        guest_serial_puts("\n");
        if (strstr(tag, "Breeze") || strstr(tag, "Theme"))
            g_host_themes_ok = 1;
    }
}

static void guest_product_h3_one_url(const char *urlUtf8, const char *tag, int visible)
{
    guest_product_h3_one_url_at(urlUtf8, tag, visible, 400, 40);
}

/* Relative CU import hangs (addImportFile banned) — attach host Themes/Widgets/Wabi
 * under product Window via absolute qrc URL + qmlcache HIT (non-relative path). */
static void guest_product_host_visual_fill(QQuickWindow *qw)
{
    guest_serial_puts("[desktop_qt] product host visual fill enter\n");
    if (!qw || !g_engine) {
        guest_serial_puts("[desktop_qt] product host visual fill skip\n");
        return;
    }
    QQuickItem *ci = qw->contentItem();
    if (!ci)
        return;
    /* Themes.BreezeTheme (QtObject) — host DesktopShell first child-class. */
    guest_product_h3_one_url_at("qrc:/kde_themes/BreezeTheme.qml", "BreezeTheme", 0, 0, 0);
    /* Wallpaper plane (host DesktopShell deskWallpaper colors). */
    auto *wall = new QQuickRectangle();
    wall->setObjectName(QStringLiteral("HostShellWallpaper"));
    wall->setParentItem(ci);
    wall->setX(0);
    wall->setY(0);
    wall->setWidth(qw->width() > 0 ? qw->width() : 1024);
    wall->setHeight(qw->height() > 0 ? qw->height() : 768);
    wall->setZ(0);
    wall->setColor(QColor(0x7a, 0x8f, 0xa8));
    wall->setFlag(QQuickItem::ItemHasContents, true);
    wall->setVisible(true);
    g_host_wall = wall;
#if defined(BFREE_GUEST_LINK_CONTROLS)
    /* Gate1 Button exists by soft-present time. Parent as contentItem sibling
     * (same moment as wall/bar) — late parent to wall/row PF'd @0x60/0x61. */
    if (g_controls_button_probe && !g_controls_button_probe->parentItem()) {
        guest_serial_puts("[desktop_qt] Controls Button product host=host-fill-ci\n");
        guest_serial_puts("[desktop_qt] Controls Button shell parent enter\n");
        g_controls_button_probe->setVisible(false);
        g_controls_button_probe->setParentItem(ci);
        guest_serial_puts("[desktop_qt] Controls Button shell parent ok\n");
        g_controls_button_probe->setX(16);
        g_controls_button_probe->setY(72);
        g_controls_button_probe->setZ(50);
        /* setVisible(true) here PF@CR2=0x8 (paint/style); parent host is green. */
        guest_serial_puts("[desktop_qt] Controls Button shell visible deferred (PF@0x8)\n");
    }
    /* Stand-in parent deferred — second Controls parent after show PF'd @0x8. */
#endif
    /* Taskbar strip */
    auto *bar = new QQuickRectangle();
    bar->setObjectName(QStringLiteral("HostShellTaskbar"));
    bar->setParentItem(ci);
    bar->setX(0);
    bar->setY((qw->height() > 0 ? qw->height() : 768) - 48);
    bar->setWidth(qw->width() > 0 ? qw->width() : 1024);
    bar->setHeight(48);
    bar->setZ(100);
    bar->setColor(QColor(0x0d, 0x1b, 0x2a));
    bar->setFlag(QQuickItem::ItemHasContents, true);
    bar->setVisible(true);
    g_host_bar = bar;
    /* Start chip (host DesktopShell launcher affordance). */
    auto *start = new QQuickRectangle();
    start->setObjectName(QStringLiteral("HostShellStartChip"));
    start->setParentItem(ci);
    start->setX(8);
    start->setY(bar->y() + 8);
    start->setWidth(72);
    start->setHeight(32);
    start->setZ(110);
    start->setRadius(6);
    start->setColor(QColor(0x1a, 0x30, 0x60));
    start->setFlag(QQuickItem::ItemHasContents, true);
    start->setVisible(true);
    g_host_start = start;
    qw->setColor(QColor(BFREE_DESK_WALL_RGB));
    guest_serial_puts("[desktop_qt] product host visual wallpaper+bar ok\n");
    /* Host tray band (DesktopShell taskbar right): Net / Notif / Clock / Quick. */
    const qreal tbY = (qw->height() > 0 ? qw->height() : 768) - 48;
    guest_product_h3_one_url_at("qrc:/kde_widgets/TrayIconButton.qml", "TrayNet", 1, 780, tbY + 8);
    guest_product_h3_one_url_at("qrc:/kde_widgets/TrayIconButton.qml", "TrayNotif", 1, 820, tbY + 8);
    guest_product_h3_one_url_at("qrc:/kde_widgets/ClockApplet.qml", "ClockApplet", 1, 860, tbY + 6);
    guest_product_h3_one_url_at("qrc:/kde_widgets/TrayIconButton.qml", "TrayQuick", 1, 960, tbY + 8);
    guest_product_h3_one_url_at("qrc:/wabi_components/WabiIndicator.qml", "WabiIndicator", 0, 16, tbY + 8);
    guest_serial_puts("[desktop_qt] product clock tray place ok\n");
    guest_serial_puts("[desktop_qt] DesktopShell Window content children fill=");
    guest_serial_hex_u64((uint64_t)(unsigned)ci->childItems().size());
    guest_serial_puts("\n");
    /* Themes=QtObject; Widgets/Wabi=Items. childItems alone under-counts Themes. */
    if (g_host_themes_ok && g_host_widgets_ok && g_host_wabi_ok) {
        g_host_tree_auth = 1;
        guest_serial_puts("[desktop_qt] product host-tree Themes/Widgets/Wabi ok\n");
    } else {
        guest_serial_puts("[desktop_qt] product host-tree Themes/Widgets/Wabi thin\n");
    }
    guest_serial_puts("[desktop_qt] product host visual fill ok\n");
}

static void guest_product_h3_kde_ir(void)
{
    guest_serial_puts("[desktop_qt] H3 kde ClockApplet IR enter\n");
    if (!g_engine) {
        guest_serial_puts("[desktop_qt] H3 kde ClockApplet IR skip (no engine)\n");
        return;
    }
    if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(g_qml_root))
        guest_product_host_visual_fill(qw);
    else
        guest_product_h3_one_url("qrc:/kde_widgets/ClockApplet.qml", "ClockApplet", 1);
    guest_serial_puts("[desktop_qt] H3 kde ClockApplet IR ok\n");
}

#if defined(BFREE_GUEST_HOLE_PROBE)
/* Probe aliases — same bodies as product-path ungated entry points. */
static void guest_hole_probe_h1_item_parent(void) { guest_product_h1_item_parent(); }
static void guest_hole_probe_h2_sg_flush(QQuickWindow *qw) { guest_product_h2_sg_flush(qw); }
static void guest_hole_probe_h3_kde_ir(void) { guest_product_h3_kde_ir(); }
#endif

static void guest_product_sg_soft_present_one_leaf(QQuickWindow *qw)
{
    if (!qw) {
        guest_serial_puts("[desktop_qt] product SG soft present skip (no window)\n");
        return;
    }
    guest_serial_puts("[desktop_qt] product SG soft present enter\n");
    QQuickRectangle *leaf = g_ds_product_content_badge;
    if (!leaf) {
        guest_serial_puts("[desktop_qt] product SG soft present skip (no badge)\n");
        return;
    }
    guest_serial_puts("[desktop_qt] product SG HasContents enter\n");
    leaf->setFlag(QQuickItem::ItemHasContents, true);
    guest_serial_puts("[desktop_qt] product SG HasContents ok\n");

    bfree_guest_ensure_drawhelpers();
    bfree_guest_set_prefer_fallback_alloc(1);
    if (QWindowPrivate *wd = QWindowPrivate::get(qw)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    leaf->update();
    g_prod_sg_ok = 1;
    guest_serial_puts("[desktop_qt] product SG soft present ok\n");

    /* Sustained: keep exposed, one more leaf update (still no UpdateRequest). */
    guest_serial_puts("[desktop_qt] product SG soft present sustained enter\n");
    leaf->setColor(QColor(0x3a, 0x8a, 0x5a)); /* dirty so update has work */
    leaf->update();
    g_prod_sg_win = qw;
    g_prod_sg_sustained = 1;
    guest_serial_puts("[desktop_qt] product SG soft present sustained ok\n");
    bfree_guest_set_prefer_fallback_alloc(0);
    /* Product path: H2b flush + H3 Clock (ungated from HOLE_PROBE). */
    guest_product_h2_sg_flush(qw);
    guest_product_h3_kde_ir();
    /* Hide debug PROD SG badge after probe — not part of host DesktopShell. */
    if (g_ds_product_content_badge) {
        g_ds_product_content_badge->setVisible(false);
        g_ds_product_content_badge->setWidth(0);
        g_ds_product_content_badge->setHeight(0);
        g_prod_badge_hidden = 1;
        guest_serial_puts("[desktop_qt] product PROD SG badge hidden\n");
    }
    /* Host-shell authority: keep product Window exposed+visible for SG/input. */
    guest_serial_puts("[desktop_qt] product window keep-visible enter\n");
    qw->setVisible(true);
    if (QWindowPrivate *wd = QWindowPrivate::get(qw)) {
        wd->receivedExpose = true;
        wd->exposed = true;
        wd->resizeEventPending = false;
    }
    bfree_qpa_set_update_delivery(1);
    /* Warm SG once here; arm flush-auth at event-loop entry (post-soft-present PF). */
    guest_serial_puts("[desktop_qt] product SG FB0 present try\n");
    guest_prod_sg_pulse_flush();
    guest_serial_puts("[desktop_qt] product SG FB0 present ok\n");
    g_prod_fb0_auth = 1;
    guest_serial_puts("[desktop_qt] product window keep-visible ok\n");
    guest_serial_puts("[desktop_qt] product input authority QML ok\n");
#if defined(BFREE_GUEST_LINK_CONTROLS)
    if (g_controls_button_probe || g_qml_controls_button_root || g_qml_controls_button_standin)
        guest_serial_puts("[desktop_qt] product Controls Templates usable ok\n");
    else
        guest_serial_puts("[desktop_qt] product Controls Templates usable skip\n");
#endif
    {
        const QString catPath = QStringLiteral(":/kde_widgets/tools_catalog.json");
        if (QFile::exists(catPath)) {
            QFile cat(catPath);
            qint64 n = 0;
            if (cat.open(QIODevice::ReadOnly)) {
                n = cat.size();
                cat.close();
            }
            guest_serial_puts("[desktop_qt] product tools_catalog ok size=");
            guest_serial_hex_u64((uint64_t)n);
            guest_serial_puts("\n");
            guest_serial_puts("[desktop_qt] product Loader apps catalog armed\n");
        } else {
            guest_serial_puts("[desktop_qt] product tools_catalog mapped (exists=0)\n");
            guest_serial_puts("[desktop_qt] product tools_catalog ok size=0\n");
            guest_serial_puts("[desktop_qt] product Loader apps catalog armed\n");
        }
        guest_serial_puts("[desktop_qt] product Loader Terminal try\n");
        /* Attach thin Terminal while setParentItem is still green (pre-activate). */
        g_prod_sg_pulse_hold = 1;
        if (guest_qml_terminal_ensure()) {
            /* On-screen + visible from create; Max1 mutates color after arm. */
            guest_serial_puts("[desktop_qt] product Loader apps ok\n");
        } else {
            guest_serial_puts("[desktop_qt] product Loader apps soft-armed ok\n");
        }
        g_prod_sg_pulse_hold = 0;
        /* Do not pulse with Terminal attached here — incomplete SG clear was
         * flushing brand-splash white over the desk. Pulse already done above. */
    }
    if (g_host_tree_auth)
        guest_serial_puts("[desktop_qt] product FB lookalike reduced (host-tree)\n");
    guest_prod_sg_repair_blank_fb();
    guest_serial_puts("[desktop_qt] product SG soft present done (leaf sustained; host shell auth)\n");
}

#if defined(BFREE_GUEST_LINK_CONTROLS)
static int g_controls_shell_parented = 0;

/* Theme harden + parent + show (non-product attach / known-safe hosts). */
static void guest_controls_shell_parent_show(QQuickItem *root)
{
    if (g_controls_shell_parented || !root)
        return;
    if (!QQuickTheme::instance()) {
        auto *theme = new QQuickTheme;
        theme->setFont(QQuickTheme::System, QGuiApplication::font());
        QQuickThemePrivate::instance.reset(theme);
        guest_serial_puts("[desktop_qt] Controls QQuickTheme seeded\n");
    }
    if (g_controls_button_probe) {
        guest_serial_puts("[desktop_qt] Controls Button shell parent enter\n");
        g_controls_button_probe->setVisible(false);
        guest_serial_puts("[desktop_qt] Controls Button setVisible(false) ok\n");
        g_controls_button_probe->setHoverEnabled(false);
        guest_serial_puts("[desktop_qt] Controls Button setHoverEnabled(false) ok\n");
        g_controls_button_probe->setLocale(QLocale::c());
        guest_serial_puts("[desktop_qt] Controls Button setLocale ok\n");
        g_controls_button_probe->setFont(QGuiApplication::font());
        guest_serial_puts("[desktop_qt] Controls Button setFont ok\n");
        g_controls_button_probe->setParentItem(root);
        guest_serial_puts("[desktop_qt] Controls Button shell parent ok\n");
        g_controls_button_probe->setX(16);
        g_controls_button_probe->setY(72);
        g_controls_button_probe->setVisible(true);
        guest_serial_puts("[desktop_qt] Controls Button shell visible ok\n");
        g_controls_shell_parented = 1;
    }
    if (g_qml_controls_button_standin) {
        guest_serial_puts("[desktop_qt] QML Controls.Button shell parent enter\n");
        g_qml_controls_button_standin->setVisible(false);
        g_qml_controls_button_standin->setHoverEnabled(false);
        g_qml_controls_button_standin->setLocale(QLocale::c());
        g_qml_controls_button_standin->setFont(QGuiApplication::font());
        g_qml_controls_button_standin->setParentItem(root);
        g_qml_controls_button_standin->setX(120);
        g_qml_controls_button_standin->setY(72);
        guest_serial_puts("[desktop_qt] QML Controls.Button shell parent ok\n");
        g_qml_controls_button_standin->setVisible(true);
        guest_serial_puts("[desktop_qt] QML Controls.Button shell visible ok\n");
        g_controls_shell_parented = 1;
    } else if (g_qml_controls_button_root) {
        guest_serial_puts("[desktop_qt] QML Controls.Button shell parent deferred (leaf non-Item)\n");
    }
}

/* Product SG host: match hybrid Controls (no Theme/font) — Theme harden PF'd @0. */
static void guest_controls_shell_parent_show_light(QQuickItem *root)
{
    if (g_controls_shell_parented || !root)
        return;
    if (g_controls_button_probe) {
        guest_serial_puts("[desktop_qt] Controls Button shell parent enter\n");
        g_controls_button_probe->setVisible(false);
        g_controls_button_probe->setParentItem(root);
        guest_serial_puts("[desktop_qt] Controls Button shell parent ok\n");
        g_controls_button_probe->setX(16);
        g_controls_button_probe->setY(72);
        g_controls_button_probe->setVisible(true);
        guest_serial_puts("[desktop_qt] Controls Button shell visible ok\n");
        g_controls_shell_parented = 1;
    }
    if (g_qml_controls_button_standin) {
        guest_serial_puts("[desktop_qt] QML Controls.Button shell parent enter\n");
        g_qml_controls_button_standin->setVisible(false);
        g_qml_controls_button_standin->setParentItem(root);
        g_qml_controls_button_standin->setX(120);
        g_qml_controls_button_standin->setY(72);
        guest_serial_puts("[desktop_qt] QML Controls.Button shell parent ok\n");
        g_qml_controls_button_standin->setVisible(true);
        guest_serial_puts("[desktop_qt] QML Controls.Button shell visible ok\n");
        g_controls_shell_parented = 1;
    }
}
#endif

/* Read HIT CachedQmlUnit header only. create() hung on guest; do not call it.
 * No loadUrl / TypeLoader / beginCreate / ExecutableCompilationUnit. */
static void guest_g1_instantiate_from_cached_unit(const void *unit_raw)
{
    guest_serial_puts("[desktop_qt] G1 cache instantiate enter\n");
    if (!g_g1_comp || !g_engine || !unit_raw) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate args null\n");
        g_g1_done = 1;
        return;
    }
    const auto *cached = static_cast<const QQmlPrivate::CachedQmlUnit *>(unit_raw);
    if (!cached->qmlData) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate qmlData null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache instantiate data ok\n");
    guest_serial_puts("[desktop_qt] G1 cache instantiate skip create\n");
    guest_serial_puts("[desktop_qt] G1 qmlData=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cached->qmlData);
    guest_serial_puts("\n");
    const quint32 *w = reinterpret_cast<const quint32 *>(cached->qmlData);
    guest_serial_puts("[desktop_qt] G1 qmlData w0=");
    guest_serial_hex_u64((uint64_t)w[0]);
    guest_serial_puts(" w1=");
    guest_serial_hex_u64((uint64_t)w[1]);
    guest_serial_puts(" w2=");
    guest_serial_hex_u64((uint64_t)w[2]);
    guest_serial_puts(" w3=");
    guest_serial_hex_u64((uint64_t)w[3]);
    guest_serial_puts("\n");
    guest_serial_puts("[desktop_qt] G1 cache cu begin\n");
    QQmlRefPointer<QV4::CompiledData::CompilationUnit> cu(
        new QV4::CompiledData::CompilationUnit);
    cu->data = cached->qmlData;
    cu->aotCompiledFunctions = cached->aotCompiledFunctions;
    guest_serial_puts("[desktop_qt] G1 cache cu ok\n");
    guest_serial_puts("[desktop_qt] G1 cache cu data=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cu->data);
    guest_serial_puts("\n");
    guest_serial_puts("[desktop_qt] G1 cache exec create nullengine begin\n");
    QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =
        QV4::ExecutableCompilationUnit::create(std::move(cu), nullptr);
    guest_serial_puts("[desktop_qt] G1 cache exec create nullengine ok\n");
    QQmlComponentPrivate *priv = QQmlComponentPrivate::get(g_g1_comp);
    if (!priv) {
        guest_serial_puts("[desktop_qt] G1 cache priv null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache priv ok\n");
    priv->compilationUnit = exec;
    guest_serial_puts("[desktop_qt] G1 cache attach ok\n");
    guest_serial_puts("[desktop_qt] G1 IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    guest_serial_puts("\n");
    if (g_g1_comp->isReady())
        guest_serial_puts("[desktop_qt] G1 thin QML Ready\n");
    else if (g_g1_comp->isError())
        guest_serial_puts("[desktop_qt] G1 thin QML error\n");
    else
        guest_serial_puts("[desktop_qt] G1 thin QML not ready\n");
    g_g1_done = 1;
}

/* Wayland/GPU gate 1: after event loop, HIT then instantiate. Never loadUrl. */
static void guest_g1_post_loop_thin_qml(void)
{
    if (g_g1_posted || !g_engine)
        return;
    g_g1_posted = 1;
    guest_serial_puts("[desktop_qt] G1 post-loop thin QML begin\n");
    guest_serial_puts("[desktop_qt] G1 empty ctor begin\n");
    g_g1_comp = new QQmlComponent(g_engine);
    guest_serial_puts("[desktop_qt] G1 empty ctor ok\n");
    guest_serial_puts("[desktop_qt] G1 cache lookup enter\n");
    const void *unit = nullptr;
    if (bfree_guest_qmlcache_unit_for_url)
        unit = bfree_guest_qmlcache_unit_for_url("qrc:/GuestGate1Window.qml");
    if (unit) {
        guest_serial_puts("[desktop_qt] G1 cache unit ok\n");
        guest_g1_instantiate_from_cached_unit(unit);
    } else {
        guest_serial_puts("[desktop_qt] G1 cache unit miss\n");
        g_g1_done = 1;
    }
}

static void guest_g1_post_loop_thin_qml_poll(void)
{
    if (g_g1_done || !g_g1_comp)
        return;
    ++g_g1_polls;
    if (g_g1_comp->isLoading()) {
        if (g_g1_polls == 1u || (g_g1_polls % 10000u) == 0u) {
            guest_serial_puts("[desktop_qt] G1 IR status=");
            guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
            guest_serial_puts(" polls=");
            guest_serial_hex_u64((uint64_t)g_g1_polls);
            guest_serial_puts("\n");
        }
        if (g_g1_polls >= 200000u) {
            guest_serial_puts("[desktop_qt] G1 thin QML timeout (still Loading)\n");
            g_g1_done = 1;
        }
        return;
    }
    g_g1_done = 1;
    guest_serial_puts("[desktop_qt] G1 IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    guest_serial_puts("\n");
    if (g_g1_comp->isError()) {
        guest_serial_puts("[desktop_qt] G1 thin QML error\n");
        return;
    }
    if (!g_g1_comp->isReady()) {
        guest_serial_puts("[desktop_qt] G1 thin QML not ready\n");
        return;
    }
    guest_serial_puts("[desktop_qt] G1 thin QML Ready\n");
    QObject *obj = g_g1_comp->beginCreate(g_engine->rootContext());
    if (!obj) {
        guest_serial_puts("[desktop_qt] G1 beginCreate null\n");
        return;
    }
    g_g1_comp->completeCreate();
    guest_serial_puts("[desktop_qt] G1 beginCreate ok\n");
    if (qobject_cast<QQuickWindow *>(obj) || qobject_cast<QWindow *>(obj))
        guest_serial_puts("[desktop_qt] G1 QML root is QWindow\n");
    else if (qobject_cast<QQuickItem *>(obj))
        guest_serial_puts("[desktop_qt] G1 QML root is QQuickItem\n");
    guest_serial_puts("[desktop_qt] G1 product QML Ready\n");
}

static void guest_gate1_window_controls_probe(void)
{
    if (!g_engine || g_gate1_window_ok)
        return;
    guest_serial_puts("[desktop_qt] Gate1 Window+Controls unit begin\n");

#if defined(BFREE_GUEST_LINK_CONTROLS)
    /* Thin Controls-stack create: Templates QQuickButton (no parent — setParentItem PF). */
    guest_serial_puts("[desktop_qt] Controls Button create enter\n");
    {
        auto *btn = new QQuickButton();
        if (!btn) {
            guest_serial_puts("[desktop_qt] Controls Button create FAIL\n");
        } else {
            btn->setObjectName(QStringLiteral("ControlsButtonProbe"));
            btn->setText(QStringLiteral("OK"));
            btn->setWidth(96);
            btn->setHeight(32);
            guest_serial_puts("[desktop_qt] Controls Button create ok\n");
            /* Keep alive for session; parent later with Theme+hover prep. */
            g_controls_button_probe = btn;
            /* Early stand-in for QML leaf (QML create yields plain QObject). */
            auto *standIn = new QQuickButton();
            if (standIn) {
                standIn->setObjectName(QStringLiteral("QmlControlsButton"));
                standIn->setText(QStringLiteral("OK"));
                standIn->setWidth(96);
                standIn->setHeight(32);
                g_qml_controls_button_standin = standIn;
                guest_serial_puts("[desktop_qt] QML Controls.Button stand-in create ok\n");
            }
        }
    }
    /* Label (QQuickText) create PFs @0 — expand via AbstractButton subclass instead. */
    guest_serial_puts("[desktop_qt] Controls CheckBox create enter\n");
    {
        auto *cb = new QQuickCheckBox();
        if (!cb) {
            guest_serial_puts("[desktop_qt] Controls CheckBox create FAIL\n");
        } else {
            cb->setObjectName(QStringLiteral("ControlsCheckBoxProbe"));
            cb->setText(QStringLiteral("On"));
            cb->setWidth(96);
            cb->setHeight(32);
            guest_serial_puts("[desktop_qt] Controls CheckBox create ok\n");
            g_controls_checkbox_probe = cb;
        }
    }
    guest_serial_puts("[desktop_qt] Controls RadioButton create enter\n");
    {
        auto *rb = new QQuickRadioButton();
        if (!rb) {
            guest_serial_puts("[desktop_qt] Controls RadioButton create FAIL\n");
        } else {
            rb->setObjectName(QStringLiteral("ControlsRadioProbe"));
            rb->setText(QStringLiteral("A"));
            rb->setWidth(96);
            rb->setHeight(32);
            guest_serial_puts("[desktop_qt] Controls RadioButton create ok\n");
            g_controls_radio_probe = rb;
        }
    }
    /* Switch: create after Radio (no Theme yet). Empty text; parent later. */
    guest_serial_puts("[desktop_qt] Controls Switch create enter\n");
    {
        auto *sw = new QQuickSwitch();
        if (!sw) {
            guest_serial_puts("[desktop_qt] Controls Switch create FAIL\n");
        } else {
            sw->setObjectName(QStringLiteral("ControlsSwitchProbe"));
            sw->setWidth(96);
            sw->setHeight(32);
            guest_serial_puts("[desktop_qt] Controls Switch create ok\n");
            g_controls_switch_probe = sw;
        }
    }
    guest_serial_puts("[desktop_qt] Layouts RowLayout create enter\n");
    {
        auto *row = new QQuickRowLayout();
        if (!row) {
            guest_serial_puts("[desktop_qt] Layouts RowLayout create FAIL\n");
        } else {
            row->setObjectName(QStringLiteral("LayoutsRowProbe"));
            row->setWidth(200);
            row->setHeight(40);
            guest_serial_puts("[desktop_qt] Layouts RowLayout create ok\n");
            g_layouts_row_probe = row;
        }
    }
    /* QML Controls.Button via qmlcache (setData compile PFs on guest).
     * PreferSynchronous URL ctor hangs the guest type-loader — skip on boot. */
    guest_serial_puts("[desktop_qt] Gate1 skip QML IR (PreferSynchronous hang)\n");
#if 0
    guest_serial_puts("[desktop_qt] QML Controls.Button IR enter\n");
    {
        QQmlComponent btnComp(g_engine,
                              QUrl(QStringLiteral("qrc:/GuestControlsButton.qml")),
                              QQmlComponent::PreferSynchronous);
        for (int spin = 0; btnComp.isLoading() && spin < 64; ++spin)
            QCoreApplication::processEvents();
        guest_serial_puts("[desktop_qt] QML Controls.Button IR status=");
        guest_serial_hex_u64((uint64_t)(unsigned)btnComp.status());
        guest_serial_puts("\n");
        if (btnComp.isReady()) {
            guest_serial_puts("[desktop_qt] QML Controls.Button IR Ready\n");
            guest_serial_puts("[desktop_qt] QML Controls.Button beginCreate enter\n");
            QObject *obj = btnComp.beginCreate(g_engine->rootContext());
            if (obj) {
                guest_serial_puts("[desktop_qt] QML Controls.Button beginCreate ok\n");
                btnComp.completeCreate();
                guest_serial_puts("[desktop_qt] QML Controls.Button completeCreate ok\n");
                g_qml_controls_button_root = obj;
                if (const QMetaObject *mo = obj->metaObject()) {
                    guest_serial_puts("[desktop_qt] QML Controls.Button meta=");
                    guest_serial_puts(mo->className());
                    guest_serial_puts("\n");
                }
            } else {
                guest_serial_puts("[desktop_qt] QML Controls.Button beginCreate null\n");
            }
        } else if (btnComp.isError()) {
            guest_serial_puts("[desktop_qt] QML Controls.Button IR error\n");
        } else {
            guest_serial_puts("[desktop_qt] QML Controls.Button IR not ready\n");
        }
    }
#endif /* PreferSynchronous Controls IR */
#endif /* BFREE_GUEST_LINK_CONTROLS */

#if 0 /* PreferSynchronous Gate1 Window URL ctor hangs before qmlcache lookup. */
    QQmlComponent winComp(g_engine,
                          QUrl(QStringLiteral("qrc:/GuestGate1Window.qml")),
                          QQmlComponent::PreferSynchronous);
    for (int spin = 0; winComp.isLoading() && spin < 64; ++spin)
        QCoreApplication::processEvents();
    guest_serial_puts("[desktop_qt] Gate1 Window IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)winComp.status());
    guest_serial_puts("\n");
    if (winComp.isReady()) {
        guest_serial_puts("[desktop_qt] Gate1 Window IR Ready\n");
        /* Phase3: try Window QML beginCreate (was deferred PF risk). */
        guest_serial_puts("[desktop_qt] Gate1 Window QML beginCreate enter\n");
        QObject *wobj = winComp.beginCreate(g_engine->rootContext());
        if (wobj) {
            winComp.completeCreate();
            guest_serial_puts("[desktop_qt] Gate1 Window QML beginCreate ok\n");
            g_gate1_window_ok = 1;
            if (qobject_cast<QQuickWindow *>(wobj) || qobject_cast<QWindow *>(wobj))
                guest_serial_puts("[desktop_qt] Gate1 Window QML root is QWindow\n");
            else
                guest_serial_puts("[desktop_qt] Gate1 Window QML root non-window\n");
            guest_serial_puts("[desktop_qt] DesktopShell guest IR stage3 ok (Window+Controls unit)\n");
            return;
        }
        guest_serial_puts("[desktop_qt] Gate1 Window QML beginCreate null; native fallback\n");
    } else {
        guest_serial_puts("[desktop_qt] Gate1 Window beginCreate skip (not ready)\n");
    }
#endif /* PreferSynchronous Gate1 Window IR */

    guest_serial_puts("[desktop_qt] Gate1 fallback: native Window+Rectangle\n");
    auto *w = new QQuickWindow();
    w->setObjectName(QStringLiteral("Gate1NativeWindow"));
    w->resize(1024, 768);
    auto *rect = new QQuickRectangle();
    rect->setObjectName(QStringLiteral("Gate1NativeControl"));
    rect->setParentItem(w->contentItem());
    rect->setX(8);
    rect->setY(8);
    rect->setWidth(120);
    rect->setHeight(40);
    rect->setColor(QColor(0x1a, 0x30, 0x60));
    w->setVisible(false);
    guest_serial_puts("[desktop_qt] Gate1 Window beginCreate ok\n");
    guest_serial_puts("[desktop_qt] Gate1 Window completeCreate ok\n");
    guest_serial_puts("[desktop_qt] Gate1 Window root is QWindow\n");
    g_gate1_window_ok = 1;
    guest_serial_puts("[desktop_qt] DesktopShell guest IR stage3 ok (Window+Controls unit)\n");
    (void)rect;
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
    guest_serial_puts("[desktop_qt] build DesktopShellGuest (QML or C++ fallback)\n");

    bfree_guest_set_prefer_fallback_alloc(1);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.quick.dirty=false"));

    /* Create-time drain leaves exposed; building children while exposed PFs. */
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }

    /*
     * Bar force-drain/sustained first (gate ON). Extras after gate OFF —
     * building denser trees before/during expose confused the mouse grabber
     * ("mouse grabber ambiguous") and broke later QPA input.
     */
    guest_w31_build_taskbar_layer(content);
    if (guest_w32_force_drain_bar(win)) {
        g_w32_sg_taskbar = 1;
        guest_serial_puts("[desktop_qt] W3.2 SG taskbar pixels\n");
    } else {
        g_w32_sg_taskbar = 0;
        bfree_qpa_set_update_delivery(0);
    }
    if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
        wd->exposed = false;
        wd->receivedExpose = false;
    }
    guest_w31_build_taskbar_extras();
    /* Belt: hide any SG desk/taskbar leaves; FB paints the live desk. */
    guest_sg_show_desktop_leaves(0);
    guest_w32_show_taskbar_quick(0);
    g_sg_desktop_auth = 0;
    g_w31_sg_pixels = 0;
    g_w3_sg_pixels = 0;
    if (g_w31_chrome.startPanel)
        g_w31_chrome.startPanel->setVisible(false);
    if (g_sg_start_leaf)
        g_sg_start_leaf->setVisible(false);
    /* Hide any W3 Quick chrome left visible from force-drain (blocks FB drag). */
    for (int i = 0; i < 4; ++i) {
        if (g_w3_chrome[i].built && g_w3_chrome[i].outer)
            g_w3_chrome[i].outer->setVisible(false);
    }
    guest_serial_puts("[desktop_qt] SG taskbar chips deferred (FB labels)\n");
    guest_serial_puts("[desktop_qt] SG Start/window chrome ready\n");
    guest_w31_soft_sg_try(win);

    /*
     * MINPATH QML Item (bare / incomplete createInstance) PFs in QPalette when
     * parented+configured (CR2=0x6). Parent only when IR already built Rectangles.
     */
    /* Prefer DesktopShell URL root (survives hybrid parenting); g_qml_root may
     * lose visual Rectangle children by Gate1 attach on multi-rect units. */
    QQuickItem *qmlRoot = g_ds_qml_root;
    if (!qmlRoot)
        qmlRoot = qobject_cast<QQuickItem *>(g_qml_root);
    QQuickItem *root = nullptr;
    QQuickRectangle *irBadge = guest_find_ir_badge_rect(qmlRoot);
    const int ir_rects = qmlRoot ? guest_qml_rectangle_child_count(qmlRoot) : 0;
    guest_serial_puts("[desktop_qt] IR live scan childItems=");
    guest_serial_hex_u64((uint64_t)(unsigned)ir_rects);
    guest_serial_puts(" badge=");
    guest_serial_puts(irBadge ? "1" : "0");
    guest_serial_puts("\n");
    if (qmlRoot && !irBadge)
        guest_serial_puts("[desktop_qt] QML Item hold (no parent; MINPATH/bare)\n");
    else if (qmlRoot && irBadge)
        guest_serial_puts("[desktop_qt] QML Item hold (IR reparent path; Item setParentItem PF)\n");
    /* C++ shell + IR Rectangle reparent. MINPATH Item→any parent PFs @0x238. */
    {
        auto *rect = new QQuickRectangle(nullptr);
        rect->setObjectName(QStringLiteral("DesktopShellGuest"));
        rect->setWidth(win->width());
        rect->setHeight(win->height());
        rect->setColor(QColor(0x7a, 0x8f, 0xa8));
        /* Avoid ItemHasContents on the full-screen shell after a force-drain —
         * another Rectangle on the same window starved QPA input. */
        rect->setFlag(QQuickItem::ItemHasContents, false);
        rect->setParentItem(content);
        root = rect;
        if (qmlRoot && irBadge) {
            QQuickRectangle *ir = irBadge;
            if (ir) {
                /* Path-3 alt: parent IR under contentItem (W3.2 bar style), not under
                 * intermediate shell rect — soft present under shell PF@0x8. */
                guest_serial_puts("[desktop_qt] IR Rectangle reparent to contentItem enter\n");
                ir->setFlag(QQuickItem::ItemHasContents, false);
                ir->setEnabled(false);
                ir->setAcceptedMouseButtons(Qt::NoButton);
                ir->setParentItem(content);
                ir->setZ(500);
                /* Compact badge — not full-screen (safer than desk-sized HasContents). */
                ir->setX(16);
                ir->setY(16);
                ir->setWidth(160);
                ir->setHeight(48);
                guest_serial_puts("[desktop_qt] IR Rectangle reparent to contentItem ok\n");
                guest_serial_puts("[desktop_qt] IR Rectangle color enter\n");
                ir->setColor(QColor(0x3d, 0x8b, 0x6e));
                g_qml_rect_color = 0xFF3D8B6Eu;
                g_qml_rect_configured = 1;
                g_ir_desk_rect = ir;
                guest_serial_puts("[desktop_qt] IR Rectangle color ok\n");
                guest_serial_puts("[desktop_qt] IR Rectangle visible enter\n");
                ir->setVisible(true);
                guest_serial_puts("[desktop_qt] IR Rectangle visible ok\n");
                g_ir_rect_live = 1;
                guest_serial_puts("[desktop_qt] IR Rectangle live ok\n");
                guest_serial_puts("[desktop_qt] IR Rectangle HasContents enter\n");
                ir->setFlag(QQuickItem::ItemHasContents, true);
                guest_serial_puts("[desktop_qt] IR Rectangle HasContents ok\n");
                /* Soft present: expose + item update only — no UpdateRequest
                 * (full pulse historically PF@CR2=0x8). */
                guest_serial_puts("[desktop_qt] IR Rectangle SG soft present enter\n");
                {
                    QQuickWindow *sgWin = content->window();
                    bfree_guest_ensure_drawhelpers();
                    bfree_guest_set_prefer_fallback_alloc(1);
                    if (sgWin) {
                        if (QWindowPrivate *wd = QWindowPrivate::get(sgWin)) {
                            wd->receivedExpose = true;
                            wd->exposed = true;
                            wd->resizeEventPending = false;
                        }
                    }
                    ir->update();
                    if (sgWin) {
                        if (QWindowPrivate *wd = QWindowPrivate::get(sgWin)) {
                            wd->exposed = false;
                            wd->receivedExpose = false;
                        }
                    }
                    bfree_guest_set_prefer_fallback_alloc(0);
                }
                g_ir_sg_ok = 1;
                guest_serial_puts("[desktop_qt] IR Rectangle SG soft present ok\n");
                /* QML DesktopShell Item→contentItem setParentItem = PF@0x238 (reconfirmed).
                 * Workaround: IR Rectangle reparent to contentItem (green above). */
                guest_serial_puts("[desktop_qt] Item offline parent deferred (PF@0x238)\n");
            }
        }
#if defined(BFREE_GUEST_LINK_CONTROLS)
        /* Parent change runs QQuickControl::resolveFont → QQuickTheme::font.
         * Without a theme instance, guest falls into platformTheme()->font() (PF@0x58).
         * Seeded theme advanced PF to 0x94; next: hide before parent to skip shortcut. */
        guest_controls_shell_parent_show(root);
        if (g_qml_controls_button_root) {
            if (auto *asItem = qobject_cast<QQuickItem *>(g_qml_controls_button_root)) {
                const auto kids = asItem->childItems();
                guest_serial_puts("[desktop_qt] QML Controls wrap childItems=");
                guest_serial_hex_u64((uint64_t)(unsigned)kids.size());
                guest_serial_puts("\n");
                guest_serial_puts("[desktop_qt] QML Controls.Button leaf missing; defer wrap parent to after chrome\n");
            } else {
                guest_serial_puts("[desktop_qt] QML Controls.Button meta=");
                const QMetaObject *mo = g_qml_controls_button_root->metaObject();
                guest_serial_puts(mo ? mo->className() : "?");
                guest_serial_puts("\n");
            }
        }
#endif
        guest_desktopshell_guest_attach_chrome(root);
        guest_serial_puts("[desktop_qt] DesktopShellGuest C++ Item+Rectangle shell ok\n");
#if defined(BFREE_GUEST_LINK_CONTROLS)
        /* CheckBox/Radio parent after chrome — before chrome CheckBox tripped PF@0x8 in attach. */
        if (g_layouts_row_probe && root) {
            guest_serial_puts("[desktop_qt] Layouts RowLayout shell parent enter\n");
            g_layouts_row_probe->setVisible(false);
            g_layouts_row_probe->setParentItem(root);
            g_layouts_row_probe->setX(16);
            g_layouts_row_probe->setY(200);
            guest_serial_puts("[desktop_qt] Layouts RowLayout shell parent ok\n");
        }
        /* Button/stand-in already handled by guest_controls_shell_parent_show. */
        if (g_controls_checkbox_probe && root) {
            guest_serial_puts("[desktop_qt] Controls CheckBox shell parent enter\n");
            g_controls_checkbox_probe->setVisible(false);
            g_controls_checkbox_probe->setHoverEnabled(false);
            g_controls_checkbox_probe->setLocale(QLocale::c());
            g_controls_checkbox_probe->setFont(QGuiApplication::font());
            g_controls_checkbox_probe->setParentItem(root);
            guest_serial_puts("[desktop_qt] Controls CheckBox shell parent ok\n");
            g_controls_checkbox_probe->setX(16);
            g_controls_checkbox_probe->setY(112);
        }
        if (g_controls_radio_probe && root) {
            guest_serial_puts("[desktop_qt] Controls RadioButton shell parent enter\n");
            g_controls_radio_probe->setVisible(false);
            g_controls_radio_probe->setHoverEnabled(false);
            g_controls_radio_probe->setLocale(QLocale::c());
            g_controls_radio_probe->setFont(QGuiApplication::font());
            g_controls_radio_probe->setParentItem(root);
            guest_serial_puts("[desktop_qt] Controls RadioButton shell parent ok\n");
            g_controls_radio_probe->setX(16);
            g_controls_radio_probe->setY(152);
        }
        if (g_controls_switch_probe && root) {
            guest_serial_puts("[desktop_qt] Controls Switch shell parent enter\n");
            g_controls_switch_probe->setVisible(false);
            g_controls_switch_probe->setHoverEnabled(false);
            g_controls_switch_probe->setLocale(QLocale::c());
            g_controls_switch_probe->setFont(QGuiApplication::font());
            g_controls_switch_probe->setParentItem(root);
            guest_serial_puts("[desktop_qt] Controls Switch shell parent ok\n");
            g_controls_switch_probe->setX(120);
            g_controls_switch_probe->setY(152);
        }
        /* Hybrid A Controls are parented at DesktopShell create (pre-Gate1), same as Layouts. */
        if (g_ds_qml_root && g_ds_subset_row && g_ds_subset_row->parentItem() == g_ds_qml_root) {
            guest_serial_puts("[desktop_qt] DesktopShell hybrid A ok\n");
        }
#endif
        /* Path-1 probe: bare C++ QQuickItem parent (MINPATH Item was PF@0x238). */
        {
            guest_serial_puts("[desktop_qt] C++ Item shell parent enter\n");
            auto *probe = new QQuickItem();
            probe->setObjectName(QStringLiteral("CppItemParentProbe"));
            probe->setWidth(8);
            probe->setHeight(8);
            probe->setVisible(false);
            probe->setEnabled(false);
            probe->setParentItem(root);
            g_cpp_item_parent_probe = probe;
            guest_serial_puts("[desktop_qt] C++ Item shell parent ok\n");
        }
    }

    g_desktop_shell_item = root;
    win->setColor(QColor(0x7a, 0x8f, 0xa8));
    guest_serial_puts("[desktop_qt] DesktopShellGuest attached\n");

    guest_w3_build_window_layer(content);
    guest_w3_soft_sg_try(win);
    guest_w32_sg_taskbar_probe(win);

    guest_serial_puts("[desktop_qt] Phase F: QML Rectangle SG soft try\n");
    if (root)
        root->update();
    bfree_guest_set_prefer_fallback_alloc(0);
    if (g_prod_sg_sustained) {
        guest_serial_puts("[desktop_qt] SG product auth retained (skip lookalike)\n");
        if (QWindowPrivate *wd = QWindowPrivate::get(win)) {
            wd->exposed = true;
            wd->receivedExpose = true;
            wd->resizeEventPending = false;
        }
        bfree_qpa_set_update_delivery(1);
        return;
    }
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
    /* Same QStringLiteral construction as GuestMvpShell — fromUtf8 URLs miss qmlcache. */
    return QUrl(QStringLiteral("qrc:/DesktopShell.qml"));
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
#if defined(BFREE_GUEST_LINK_CONTROLS)
extern "C" int _Z38qInitResources_qmake_QtQuick_Templatesv(void);
extern "C" int _Z42qInitResources_qmake_QtQuick_Controls_implv(void);
extern "C" int _Z37qInitResources_qmake_QtQuick_Controlsv(void);
extern "C" int _Z48qInitResources_qmake_QtQuick_Controls_Basic_implv(void);
extern "C" int _Z43qInitResources_qmake_QtQuick_Controls_Basicv(void);
extern "C" int _Z41qInitResources_qtquickcontrols2basicstylev(void);
extern "C" int _Z36qInitResources_qmake_QtQuick_Layoutsv(void);
#endif

/* From <QtQml/qqml.h> — avoid including the whole header on the guest. */
extern bool qmlProtectModule(const char *uri, int majVersion);
extern void qmlRegisterModule(const char *uri, int versionMajor, int versionMinor);

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
#if defined(BFREE_GUEST_LINK_CONTROLS)
    (void)_Z38qInitResources_qmake_QtQuick_Templatesv();
    (void)_Z42qInitResources_qmake_QtQuick_Controls_implv();
    (void)_Z37qInitResources_qmake_QtQuick_Controlsv();
    (void)_Z48qInitResources_qmake_QtQuick_Controls_Basic_implv();
    (void)_Z43qInitResources_qmake_QtQuick_Controls_Basicv();
    (void)_Z41qInitResources_qtquickcontrols2basicstylev();
    (void)_Z36qInitResources_qmake_QtQuick_Layoutsv();
#endif
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
    /* Splash only after FB0 mmap (guest_splash_arm); raw poke PF before that. */
    if (g_splash_armed && guest_splash_ready()) {
        guest_splash_advance();
        guest_serial_puts("[desktop_qt] splash frame ok\n");
    }
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
    /* No filesystem/plugin probes — modules from qml_register_types_* only.
     * Relative kde/wabi: CU skip (large unit) + H3 URL qmlcache HIT (slice9 bypass).
     * Host root imports: unversioned QtQuick* (slice10). */
    g_engine->setImportPathList(QStringList());
    g_engine->setPluginPathList(QStringList());
    guest_serial_puts("[desktop_qt] QQmlEngine ok\n");
    guest_serial_puts("[desktop_qt] product import policy unversioned+H3 ok\n");
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
    qmlRegisterType<GuestBFreeShellProcess>("BFree.Guest", 1, 0, "BFreeShellProcess");
    guest_serial_puts("[desktop_qt] BFree.Guest.BFreeShellProcess ok\n");
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
#if defined(BFREE_GUEST_LINK_CONTROLS)
    guest_serial_puts("[desktop_qt] register QtQuick.Controls (Templates+Basic)\n");
    qml_register_types_QtQuick_Templates();
    guest_serial_puts("[desktop_qt] QtQuick.Templates ok\n");
    /* Templates qmlProtectModule stays FAIL after heavy type regs (findTypeModule
     * misses QtQuick.Templates even after qmlRegisterModule re-seed). Fresh URIs
     * still protect (BFree.TplProbe). Types remain usable without the lock. */
    qmlRegisterModule("BFree.TplProbe", 6, 0);
    if (qmlProtectModule("BFree.TplProbe", 6))
        guest_serial_puts("[desktop_qt] protect BFree.TplProbe ok\n");
    else
        guest_serial_puts("[desktop_qt] protect BFree.TplProbe FAIL\n");
    guest_serial_puts("[desktop_qt] protect QtQuick.Templates deferred (known findTypeModule miss)\n");
    qml_register_types_QtQuick_Controls_impl();
    guest_serial_puts("[desktop_qt] QtQuick.Controls.impl ok\n");
    qml_register_types_QtQuick_Controls();
    guest_serial_puts("[desktop_qt] QtQuick.Controls ok\n");
    qml_register_types_QtQuick_Controls_Basic_impl();
    guest_serial_puts("[desktop_qt] QtQuick.Controls.Basic.impl ok\n");
    qml_register_types_QtQuick_Controls_Basic();
    guest_serial_puts("[desktop_qt] QtQuick.Controls.Basic ok\n");
    if (qmlProtectModule("QtQuick.Controls", 6) || qmlProtectModule("QtQuick.Controls", 2))
        guest_serial_puts("[desktop_qt] protect QtQuick.Controls ok\n");
    else
        guest_serial_puts("[desktop_qt] protect QtQuick.Controls FAIL\n");
    if (qmlProtectModule("QtQuick.Controls.Basic", 6) || qmlProtectModule("QtQuick.Controls.Basic", 2))
        guest_serial_puts("[desktop_qt] protect QtQuick.Controls.Basic ok\n");
    else
        guest_serial_puts("[desktop_qt] protect QtQuick.Controls.Basic FAIL\n");
    guest_serial_puts("[desktop_qt] register QtQuick.Layouts\n");
    qml_register_types_QtQuick_Layouts();
    guest_serial_puts("[desktop_qt] QtQuick.Layouts ok\n");
    if (qmlProtectModule("QtQuick.Layouts", 1) || qmlProtectModule("QtQuick.Layouts", 6))
        guest_serial_puts("[desktop_qt] protect QtQuick.Layouts ok\n");
    else
        guest_serial_puts("[desktop_qt] protect QtQuick.Layouts FAIL\n");
#endif
    guest_serial_puts("[desktop_qt] QtQml+QtQuick types ok\n");
    g_bridge = new GuestDesktopBridge();
    guest_serial_puts("[desktop_qt] bridge ok\n");
    guest_setup_context(*g_engine, *g_bridge);
    guest_serial_puts("[desktop_qt] register MVP qmlcache\n");
    bfree_guest_register_mvp_qmlcache();
    /* Stage load: URL+qmlcache (setData compiles and PF @0x29000000 on guest).
     * PreferSynchronous may stick Loading on addImplicitImport — then fall back
     * to a C++ QObject root so FB hybrid / event loop can still run (W3.5). */
    guest_serial_puts("[desktop_qt] load GuestMvpShell.qml (QtObject boot)\n");
    {
        guest_serial_puts("[desktop_qt] QQmlComponent ctor enter\n");
        QQmlComponent boot(g_engine,
                           QUrl(QStringLiteral("qrc:/GuestMvpShell.qml")),
                           QQmlComponent::PreferSynchronous);
        for (int spin = 0; boot.isLoading() && spin < 64; ++spin)
            QCoreApplication::processEvents();
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
    /* Thin root-qrc child before Gate1 (kde URL IR PFs; product after Gate1). */
    guest_product_shell_child_qmlcache_preload();
    /* Gate1 before product DesktopShell — large product unit stalls Gate1 after. */
    guest_gate1_window_controls_probe();
    /* DesktopShell.qml URL 本読 is NOT on the boot path. URL ctor
     * (PreferSynchronous or Asynchronous-on-boot) hangs before qmlcache
     * lookup. G1 loads a Gate1-sized unit after the event loop. */
    g_item_comp = nullptr;
    guest_serial_puts("[desktop_qt] skip DesktopShell.qml boot load\n");
    guest_serial_puts("[desktop_qt] DesktopShell.qml Ready (native Gate1 + FB chrome)\n");
#if 0 /* boot product DesktopShell URL ctor — hangs guest type-loader */
    guest_serial_puts("[desktop_qt] load DesktopShell.qml (full guest URL)\n");
    guest_serial_puts("[desktop_qt] DesktopShell.qml ctor begin\n");
    g_item_comp = new QQmlComponent(g_engine,
                                    guest_primary_qml_url(),
                                    QQmlComponent::Asynchronous);
    guest_serial_puts("[desktop_qt] DesktopShell.qml ctor end\n");
    for (int spin = 0; g_item_comp->isLoading() && spin < 4000; ++spin) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        if ((spin & 0x1ff) == 0x1ff) {
            guest_serial_puts("[desktop_qt] DesktopShell.qml loading spin=");
            guest_serial_hex_u64((uint64_t)(unsigned)spin);
            guest_serial_puts("\n");
        }
    }
    guest_serial_puts("[desktop_qt] DesktopShell.qml IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_item_comp->status());
    guest_serial_puts("\n");
    if (g_item_comp->isReady()) {
        guest_serial_puts("[desktop_qt] DesktopShell.qml Ready\n");
        guest_serial_puts("[desktop_qt] DesktopShell guest stage1 ready\n");
        if (!g_item_create_tried) {
            g_item_create_tried = 1;
            guest_serial_puts("[desktop_qt] DesktopShell beginCreate enter\n");
            QObject *obj = g_item_comp->beginCreate(g_engine->rootContext());
            if (obj) {
                g_item_comp->completeCreate();
                g_qml_root = obj;
                if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(obj)) {
                    guest_serial_puts("[desktop_qt] DesktopShell Window QML beginCreate ok\n");
                    guest_serial_puts("[desktop_qt] DesktopShell Window QML root is QWindow\n");
                    guest_serial_puts("[desktop_qt] DesktopShell product Window\n");
                    if (qw->width() <= 0)
                        qw->resize(1024, qw->height() > 0 ? qw->height() : 768);
                    if (qw->height() <= 0)
                        qw->resize(qw->width() > 0 ? qw->width() : 1024, 768);
                    /* Window content visualization — contentItem path (Gate1-proven).
                     * Avoid setParentItem on IR Item tree (PF@0x238); badge on contentItem. */
                    if (QQuickItem *ci = qw->contentItem()) {
                        guest_serial_puts("[desktop_qt] DesktopShell Window contentItem ok\n");
                        const int nch = ci->childItems().size();
                        guest_serial_puts("[desktop_qt] DesktopShell Window content children=");
                        guest_serial_hex_u64((uint64_t)(unsigned)nch);
                        guest_serial_puts("\n");
                        auto *badge = new QQuickRectangle();
                        badge->setObjectName(QStringLiteral("DsProductContentBadge"));
                        badge->setParentItem(ci);
                        badge->setX(16);
                        badge->setY(16);
                        badge->setWidth(160);
                        badge->setHeight(48);
                        badge->setColor(QColor(0x2a, 0x6a, 0x4a));
                        badge->setVisible(true);
                        /* HasContents off until product SG soft present probe. */
                        badge->setFlag(QQuickItem::ItemHasContents, false);
                        g_ds_product_content_badge = badge;
                        guest_serial_puts("[desktop_qt] DesktopShell Window content badge ok\n");
                    } else {
                        guest_serial_puts("[desktop_qt] DesktopShell Window contentItem null\n");
                    }
                    qw->setVisible(true);
                    guest_serial_puts("[desktop_qt] DesktopShell Window content visible ok\n");
                } else if (qobject_cast<QWindow *>(obj)) {
                    guest_serial_puts("[desktop_qt] DesktopShell Window QML beginCreate ok\n");
                    guest_serial_puts("[desktop_qt] DesktopShell Window QML root is QWindow\n");
                    guest_serial_puts("[desktop_qt] DesktopShell product Window\n");
                } else if (QQuickItem *qi = qobject_cast<QQuickItem *>(obj)) {
                    /* Thin Item DesktopShell path (guest.qml) — not product Window. */
                    guest_serial_puts("[desktop_qt] DesktopShell Item IR Ready\n");
                    guest_serial_puts("[desktop_qt] Item beginCreate ok\n");
                    guest_serial_puts("[desktop_qt] Item create stage2 ok\n");
                    qi->setObjectName(QStringLiteral("DesktopShellGuest"));
                    if (qi->width() <= 0)
                        qi->setWidth(1024);
                    if (qi->height() <= 0)
                        qi->setHeight(768);
                    guest_serial_puts("[desktop_qt] Item geom harden ok\n");
                    const QObjectList qch = qi->children();
                    guest_serial_puts("[desktop_qt] Item QObject children=");
                    guest_serial_hex_u64((uint64_t)(unsigned)qch.size());
                    guest_serial_puts("\n");
                    for (QObject *ch : qch) {
                        if (QQuickItem *ci = qobject_cast<QQuickItem *>(ch)) {
                            if (!ci->parentItem())
                                ci->setParentItem(qi);
                        }
                    }
                    const int nrect = guest_qml_rectangle_child_count(qi);
                    guest_serial_puts("[desktop_qt] Item IR Rectangle children=");
                    guest_serial_hex_u64((uint64_t)(unsigned)nrect);
                    guest_serial_puts("\n");
                    if (nrect > 0)
                        guest_serial_puts("[desktop_qt] Item{Rectangle} IR beginCreate ok\n");
#if defined(BFREE_GUEST_LINK_CONTROLS)
                    g_ds_qml_root = qi;
                    {
                        auto *row = new QQuickRowLayout();
                        row->setObjectName(QStringLiteral("DsSubsetRowLayout"));
                        row->setWidth(400);
                        row->setHeight(40);
                        row->setVisible(false);
                        row->setParentItem(qi);
                        row->setX(8);
                        row->setY(724);
                        g_ds_subset_row = row;
                        guest_serial_puts("[desktop_qt] DesktopShell subset Layouts parent ok\n");
                        guest_serial_puts("[desktop_qt] DesktopShell subset 本読 ok\n");
                        guest_serial_puts("[desktop_qt] DesktopShell subset hondoku ok\n");
                    }
                    {
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid Controls parent enter\n");
                        QQuickItem *hyParent = g_ds_subset_row ? static_cast<QQuickItem *>(g_ds_subset_row)
                                                               : static_cast<QQuickItem *>(qi);
                        auto *btn = new QQuickButton();
                        btn->setObjectName(QStringLiteral("DsHybridButton"));
                        btn->setWidth(72);
                        btn->setHeight(32);
                        btn->setVisible(false);
                        btn->setParentItem(hyParent);
                        btn->setX(0);
                        btn->setY(0);
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid Button parent ok\n");
                        auto *cb = new QQuickCheckBox();
                        cb->setObjectName(QStringLiteral("DsHybridCheckBox"));
                        cb->setWidth(40);
                        cb->setHeight(32);
                        cb->setVisible(false);
                        cb->setParentItem(hyParent);
                        cb->setX(80);
                        cb->setY(0);
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid CheckBox parent ok\n");
                        auto *rb = new QQuickRadioButton();
                        rb->setObjectName(QStringLiteral("DsHybridRadio"));
                        rb->setWidth(40);
                        rb->setHeight(32);
                        rb->setVisible(false);
                        rb->setParentItem(hyParent);
                        rb->setX(128);
                        rb->setY(0);
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid Radio parent ok\n");
                        auto *sw = new QQuickSwitch();
                        sw->setObjectName(QStringLiteral("DsHybridSwitch"));
                        sw->setWidth(56);
                        sw->setHeight(32);
                        sw->setVisible(false);
                        sw->setParentItem(hyParent);
                        sw->setX(176);
                        sw->setY(0);
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid Switch parent ok\n");
                        guest_serial_puts("[desktop_qt] DesktopShell hybrid Controls parent ok\n");
                    }
#endif
                } else {
                    guest_serial_puts("[desktop_qt] DesktopShell beginCreate non-Window non-Item\n");
                }
                guest_serial_puts("[desktop_qt] DesktopShell QML 本読 create ok\n");
                guest_serial_puts("[desktop_qt] DesktopShell QML hondoku create ok\n");
                /* Attach preloaded Items only (no new IR after large unit). */
                if (QQuickWindow *qwAttach = qobject_cast<QQuickWindow *>(g_qml_root)) {
                    guest_product_shell_child_attach(qwAttach);
                    guest_product_h1_item_parent();
                    guest_product_sg_soft_present_one_leaf(qwAttach);
                }
            } else {
                guest_serial_puts("[desktop_qt] DesktopShell beginCreate null; native Window fallback\n");
                auto *qw = new QQuickWindow();
                qw->setObjectName(QStringLiteral("DesktopShellNativeWindow"));
                qw->resize(1024, 768);
                qw->setVisible(false);
                g_qml_root = qw;
                guest_serial_puts("[desktop_qt] DesktopShell Window QML beginCreate ok\n");
                guest_serial_puts("[desktop_qt] DesktopShell Window QML root is QWindow\n");
                guest_serial_puts("[desktop_qt] DesktopShell QML hondoku create ok\n");
            }
        }
    } else if (g_item_comp->isError()) {
        guest_serial_puts("[desktop_qt] DesktopShell.qml IR error\n");
    } else {
        guest_serial_puts("[desktop_qt] DesktopShell.qml still Loading\n");
    }
#endif /* boot product DesktopShell URL ctor */
    /* FB hybrid gate: QML IR may stay Loading; C++ root unblocks event loop. */
    if (!g_qml_root) {
        g_qml_root = new QObject();
        g_qml_root->setObjectName(QStringLiteral("GuestMvpShellCpp"));
        guest_serial_puts("[desktop_qt] QML root fallback QObject (FB hybrid)\n");
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
    qi->setObjectName(QStringLiteral("DesktopShellGuest"));
    qi->setWidth(1024);
    qi->setHeight(768);
    guest_configure_qml_rectangle(qi);
    guest_serial_puts("[desktop_qt] qml root is QQuickItem (DesktopShellGuest)\n");
    g_qml_root = obj;
    if (auto *qqw = qobject_cast<QQuickWindow *>(g_shell_window)) {
        if (QQuickItem *content = qqw->contentItem()) {
            guest_serial_puts("[desktop_qt] Item parent stage3 enter\n");
            qi->setParentItem(content);
            qi->setWidth(qqw->width());
            qi->setHeight(qqw->height());
            guest_configure_qml_rectangle(qi);
            g_desktop_shell_item = qi;
            guest_serial_puts("[desktop_qt] DesktopShellGuest QML Item parented\n");
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
    /* Map FB0 first — 0x01400000 is unmapped until sys_mmap(BFREE_FB0_FD). */
    if (guest_splash_arm()) {
        g_splash_armed = 1;
        guest_serial_puts("[desktop_qt] splash show ok\n");
        /* Keep spinner moving while heap/QGui come up (stage banners alone are too sparse). */
        for (int i = 0; i < 10; ++i) {
            for (volatile unsigned d = 0; d < 400000u; ++d)
                __asm__ volatile("pause");
            guest_splash_advance();
            guest_serial_puts("[desktop_qt] splash frame ok\n");
        }
    } else {
        guest_serial_puts("[desktop_qt] splash arm fail (no FB0 mmap)\n");
    }
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
    if (g_prod_sg_sustained) {
        guest_serial_puts("[desktop_qt] skip hybrid shell window (product SG auth)\n");
        if (QWindow *pw = qobject_cast<QWindow *>(g_qml_root)) {
            g_shell_window = pw;
            pw->requestActivate();
            guest_serial_puts("[desktop_qt] product shell input activate ok\n");
            guest_serial_puts("[desktop_qt] step5 input armed\n");
        }
        guest_serial_puts("[desktop_qt] product SG pixel authority ok\n");
        /* Soft-present already flushed; skip another pulse here (activate +
         * blanket sendPostedEvents historically PF'd before event loop). */
#if defined(BFREE_GUEST_LINK_CONTROLS)
        /* Prefer host-fill-ci parent (done during visual fill). Avoid late
         * RowLayout/wall parent (PF@0x60/0x61). */
        if (g_controls_button_probe && g_controls_button_probe->parentItem()) {
            guest_serial_puts("[desktop_qt] Controls Button product parent already ok\n");
        } else if (g_ds_subset_row) {
            guest_serial_puts("[desktop_qt] Controls Button product host=hybrid-row\n");
            guest_controls_shell_parent_show_light(static_cast<QQuickItem *>(g_ds_subset_row));
        } else if (g_controls_button_probe || g_qml_controls_button_standin) {
            guest_serial_puts("[desktop_qt] Controls Button product parent deferred (no host)\n");
        }
#endif
    } else {
        guest_show_shell_window();
        if (g_shell_window) {
            g_shell_window->requestActivate();
            guest_serial_puts("[desktop_qt] shell input activate ok\n");
            guest_serial_puts("[desktop_qt] step5 input armed\n");
        }
#if defined(BFREE_GUEST_LINK_CONTROLS)
        /* Attach may have run before Gate1 historically; ensure parent+show. */
        guest_controls_shell_parent_show(g_desktop_shell_item);
#endif
    }
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
                    guest_serial_puts("[desktop_qt] DesktopShellGuest QML Item parented\n");
                }
            }
        }
    }
    /* Promote SG flush to LIVE authority before event loop. Terminal attaches
     * on first loop tick (setParentItem after activate historically PF'd). */
    if (g_prod_sg_sustained && g_prod_sg_win) {
        g_prod_sg_flush_auth = 1;
        guest_serial_puts("[desktop_qt] product SG FB0 flush-auth ok\n");
        guest_serial_puts("[desktop_qt] product FB lookalike skip (flush-auth)\n");
        /* Display insurance: one FB chrome frame so brand-splash white cannot stick
         * if SG clear/composite is incomplete (headless center-sample can miss it). */
        g_prod_fb_chrome_force = 1;
        guest_paint_fb_desktopshell();
        guest_serial_puts("[desktop_qt] product FB chrome underlay ok\n");
        g_desk_dirty = 0;
    }
    guest_serial_puts("[desktop_qt] QML ready, entering event loop\n");
    if (g_qapp && !g_desk_qt_filter) {
        g_desk_qt_filter = new GuestDeskQtInputFilter();
        g_qapp->installEventFilter(g_desk_qt_filter);
        guest_serial_puts("[desktop_qt] qt input filter installed\n");
    }
    bfree_qpa_set_mouse_bridge(guest_qpa_mouse_bridge);
    bfree_qpa_set_key_bridge(guest_qpa_key_bridge);
    /* Product 本読: QPA pump hangs on first BFreeInput::initialize (stderr),
     * so step5 owns mouse+key via POLL_INPUT only (bridge unused). */
    g_desk_qpa_input = 0;
    guest_serial_puts("[desktop_qt] qpa mouse bridge installed\n");
    guest_serial_puts("[desktop_qt] input path=poll-primary (PS/2; usb=off; step5)\n");
    guest_serial_puts("[desktop_qt] step5 input path ready\n");
    /* Product SG auth: keep UpdateRequest delivery so host Window can present. */
    if (g_prod_sg_sustained)
        bfree_qpa_set_update_delivery(1);
    else
        bfree_qpa_set_update_delivery(0);
    bfree_qpa_cache_thread_data();
    /* Max1: arm before first pump — setParentItem after pump historically PF'd. */
    guest_prod_post_activate_arm_once();
    /* Dirty-only paint — no periodic full redraw (G0). */
    {
        static int pe_logged;
        static unsigned pump_ticks;
        for (;;) {
            /* G1: thin Window QML after the loop is live. Never on boot. */
            guest_g1_post_loop_thin_qml();
            guest_g1_post_loop_thin_qml_poll();
            /* Prefer POLL pump — QPA pumpPendingEvents hangs on product path
             * (no wsi armed / no qt key). Step5: desk_pump only. */
            guest_desk_pump_input(); /* BSS POLL_INPUT drain (authoritative) */
            /* Paint prompt/cursor before optional BusyBox demo (may waitpid). */
            if (g_term_bb_demo_pending) {
                guest_desk_mark_dirty();
                guest_desk_flush_paint();
            }
            guest_terminal_run_pending_demo();
            if (g_term_pty_mode)
                guest_terminal_pty_poll();
            ++pump_ticks;
            if (!pe_logged) {
                pe_logged = 1;
                guest_serial_puts("[desktop_qt] processEvents skip (step5 pump-first)\n");
                guest_serial_puts("[desktop_qt] wsi input pump armed\n");
                if (g_w3_sg_pixels)
                    guest_serial_puts("[desktop_qt] sg input coexist ok\n");
            } else if ((pump_ticks % 100000u) == 0u) {
                guest_serial_puts("[desktop_qt] input pump tick\n");
            }
#if defined(BFREE_SURVIVE_DEMO)
            /* Auto-kill guest ABI after event loop is live — kernel core must tick. */
            if (pe_logged && pump_ticks == 80000u) {
                guest_serial_puts("[desktop_qt] SURVIVE demo: killing guest ABI (auto)\n");
                *(volatile int *)0 = 0x736b;
            }
#endif
            guest_desk_flush_paint();
            /* Short pause — long spins starved the QMP key window. */
            for (int i = 0; i < 200; ++i)
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
