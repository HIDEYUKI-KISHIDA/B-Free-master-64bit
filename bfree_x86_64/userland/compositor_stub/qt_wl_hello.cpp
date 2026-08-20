/* Tiny guest QGuiApplication client. Not desktop.elf, not libqbfree.a.
 * D2: carries compositor_stub/DesktopShell.qml as a Wayland client scene.
 * D2c: 1024×768 bits covering compositor output (not QQmlEngine).
 * D2b (BFREE_D2B_QML): QQmlEngine only. Do not beginCreate (CR2=0xC).
 * Paint GuestMvpShell layout in QImage bits, then exit_group so g1-desk
 * vfork parent can blit.
 * Do not call bfree_guest_enter_preflighted_mmap_noreturn. */
#include <new>
#include <QBackingStore>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPluginLoader>
#include <QStaticPlugin>
#include <QString>
#include <QWindow>
#include <QtPlugin>
#ifdef BFREE_D2B_QML
#include <QQmlEngine>
#include <QStringList>
#endif

extern const QT_PREPEND_NAMESPACE(QStaticPlugin) qt_static_plugin_QBfreeWlIntegrationPlugin();

extern "C" {
void bfree_guest_refresh_libc_auxv(void);
void bfree_guest_preflight_musl_heap(void);
void bfree_guest_preflight_ctor_mmap(void);
void bfree_guest_run_on_ctor_stack_hybrid(void (*fn)(void));
int bfree_guest_ensure_fallback_heap(void);
void bfree_guest_begin_hybrid_alloc(void);
char *getenv(const char *);
#ifdef BFREE_D2B_QML
void bfree_guest_qv4_preflight_arena(void);
void bfree_guest_qv4_mmap_scope_begin(void);
void bfree_guest_qv4_set_active_engine(void *);
#endif
}

static char g_prog[] = "/p8test.elf";
static char g_arg_platform[] = "-platform";
static char g_arg_wl[] = "wayland";
static char *g_qt_argv[] = {g_prog, g_arg_platform, g_arg_wl, nullptr};
static int g_qt_argc = 3;
static QGuiApplication *g_app;

/* Keep in sync with compositor_stub/DesktopShell.qml. Do not instantiate. */
static const char g_d2_qml[] =
    "import QtQuick\n"
    "Item {\n"
    "    /* D2: product desk scene carried by the stub Wayland client (p8test).\n"
    "     * Do not QQmlEngine / beginCreate this file on the guest (CR2=0xC).\n"
    "     * qt_wl_hello paints this layout into QImage bits (GuestMvpShell:\n"
    "     * wallpaper #7A8FA8, card #F8FAFC, bar #334155, EX/VW/TE tiles). */\n"
    "    width: 1024\n"
    "    height: 768\n"
    "}\n";

static void qt_hello_serial(const char *s)
{
    unsigned long n = 0;
    while (s[n]) {
        n++;
    }
    register long rax __asm__("rax") = 24;
    register long rdi __asm__("rdi") = (long)s;
    register long rsi __asm__("rsi") = (long)n;
    register long rdx __asm__("rdx") = 0;
    register long r10 __asm__("r10") = 0;
    register long r8 __asm__("r8") = 0;
    register long r9 __asm__("r9") = 0;
    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
}

/* qRegister on the exec stack PFs. BSS bump is reused by hybrid, so register
 * here on fallback/hybrid heap so QFactoryLoader's QList stays valid. */
__attribute__((noinline)) static void hello_register_plugin(void)
{
    int n;

    qt_hello_serial("[qt] plugin register on hybrid heap\n");
    qRegisterStaticPluginFunction(qt_static_plugin_QBfreeWlIntegrationPlugin());
    n = QPluginLoader::staticPlugins().size();
    qt_hello_serial("[qt] plugin registered n=");
    {
        char b[4];
        b[0] = (char)('0' + ((n / 10) % 10));
        b[1] = (char)('0' + (n % 10));
        b[2] = '\n';
        b[3] = 0;
        qt_hello_serial(b);
    }
    /* Do not call QStaticPlugin::metaData() here — QJson/CBOR parse hung
     * after n=01. QFactoryLoader will parse Keys during QGui ctor. */
    qt_hello_serial("[qt] plugin register done\n");
}

static void hello_qt_msg(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    char buf[96];
    int n;
    int i;

    if (type == QtFatalMsg) {
        qt_hello_serial("[qt] QtFatal\n");
    } else if (type == QtCriticalMsg) {
        qt_hello_serial("[qt] QtCritical\n");
    }
    n = msg.size();
    if (n > 80) {
        n = 80;
    }
    for (i = 0; i < n; i++) {
        unsigned u = (unsigned)msg.at(i).unicode();
        buf[i] = (u < 128u) ? (char)u : '?';
    }
    buf[i] = '\n';
    buf[i + 1] = 0;
    qt_hello_serial(buf);
}

static void d2_fill(unsigned *bits, int bpl, int w, int h, int x0, int y0, int rw,
                   int rh, unsigned argb)
{
    int x;
    int y;

    if (x0 < 0) {
        rw += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        rh += y0;
        y0 = 0;
    }
    if (rw <= 0 || rh <= 0 || x0 >= w || y0 >= h) {
        return;
    }
    for (y = y0; y < y0 + rh && y < h; y++) {
        unsigned *row = bits + y * bpl;
        for (x = x0; x < x0 + rw && x < w; x++) {
            row[x] = argb;
        }
    }
}

static void d2_serial_u32(unsigned v)
{
    char b[12];
    int i;
    unsigned n;

    if (v == 0) {
        qt_hello_serial("0");
        return;
    }
    n = v;
    i = 0;
    while (n && i < (int)sizeof(b) - 1) {
        b[i++] = (char)('0' + (n % 10u));
        n /= 10u;
    }
    while (i > 0) {
        char one[2];
        one[0] = b[--i];
        one[1] = 0;
        qt_hello_serial(one);
    }
}

/* D1: hello APP mmap compat reports QT_QPA_PLATFORM=wayland.
 * wrap_getenv for desktop.elf stays bfree (daily N2). Return after
 * flush — vfork waits for child exit, not exec.
 * Observed: plugin register ok, then PF CR2=0 between "before ctor" and
 * "ctor ok". desktop.elf uses fallback+hybrid then `new QGuiApplication`.
 * -fno-exceptions `new T` on a null operator new still runs T's ctor at
 * this==0 → CR2=0. Check operator new before placement-new. */
__attribute__((noinline)) static void hello_gui_session(void)
{
    void *mem;

    __asm__ volatile("andq $-16, %%rsp" ::: "rsp");
    qt_hello_serial("[qt] before QGuiApplication ctor\n");
    QCoreApplication::setSetuidAllowed(true);
    qt_hello_serial("[qt] setuid ok\n");
    bfree_guest_refresh_libc_auxv();
    (void)bfree_guest_ensure_fallback_heap();
    bfree_guest_begin_hybrid_alloc();
    qt_hello_serial("[qt] hybrid alloc armed\n");
    qInstallMessageHandler(hello_qt_msg);
    {
        const char *p = getenv("QT_QPA_PLATFORM");
        qt_hello_serial("[qt] getenv QT_QPA_PLATFORM=");
        qt_hello_serial(p ? p : "(null)");
        qt_hello_serial("\n");
        qt_hello_serial("[qt] D2 argv -platform ");
        qt_hello_serial(g_arg_wl);
        qt_hello_serial("\n");
        if (!p || p[0] != 'w' || p[1] != 'a' || p[2] != 'y') {
            qt_hello_serial("[qt] D2 QPA env not wayland (stale APP mmap compat)\n");
        }
    }
    qt_hello_serial("[qt] D2 qml bytes=");
    d2_serial_u32((unsigned)(sizeof(g_d2_qml) - 1u));
    qt_hello_serial("\n");
    hello_register_plugin();
    qt_hello_serial("[qt] before operator new\n");
    mem = ::operator new(sizeof(QGuiApplication));
    if (!mem) {
        qt_hello_serial("[qt] operator new returned 0\n");
        return;
    }
    qt_hello_serial("[qt] operator new ok\n");
    g_app = new (mem) QGuiApplication(g_qt_argc, g_qt_argv);
    qt_hello_serial("[qt] QGuiApplication ctor ok\n");

#ifdef BFREE_D2B_QML
    /* D2b: QQmlEngine on the Wayland client. No QQmlComponent, no loadUrl,
     * no qml_register_types, no beginCreate, no processEvents. */
    qt_hello_serial("[qt] D2b engine enter\n");
    bfree_guest_qv4_preflight_arena();
    qt_hello_serial("[qt] D2b qv4 preflight\n");
    bfree_guest_qv4_mmap_scope_begin();
    qt_hello_serial("[qt] D2b qv4 scope\n");
    {
        void *emem = ::operator new(sizeof(QQmlEngine));
        QQmlEngine *eng;
        if (!emem) {
            qt_hello_serial("[qt] D2b engine new 0\n");
        } else {
            qt_hello_serial("[qt] D2b engine operator new ok\n");
            eng = new (emem) QQmlEngine();
            eng->setImportPathList(QStringList());
            eng->setPluginPathList(QStringList());
            bfree_guest_qv4_set_active_engine(eng);
            qt_hello_serial("[qt] D2b engine ok\n");
        }
    }
#endif

    qt_hello_serial("[qt] window start\n");
    QWindow win;
    qt_hello_serial("[qt] window\n");
    win.setGeometry(0, 0, 1024, 768);
    win.setSurfaceType(QSurface::RasterSurface);
    QBackingStore store(&win);
    qt_hello_serial("[qt] store\n");
    win.create();
    qt_hello_serial("[qt] create\n");
    store.resize(QSize(1024, 768));
    qt_hello_serial("[qt] resize\n");
    win.show();
    qt_hello_serial("[qt] show\n");

    const QRect rect(0, 0, 1024, 768);
    store.beginPaint(rect);
    qt_hello_serial("[qt] beginPaint\n");
    /* Do not QPainter::fillRect — dummy font DB / raster engine is null
     * (PF CR2=0 right after [qt] painter). Write QImage bits. */
    {
        QImage *img = static_cast<QImage *>(store.paintDevice());
        unsigned *bits;
        int bpl;
        int w;
        int h;

        if (!img || img->isNull()) {
            qt_hello_serial("[qt] image null\n");
        } else {
            qt_hello_serial("[qt] image\n");
            bits = (unsigned *)img->bits();
            if (!bits) {
                qt_hello_serial("[qt] bits 0\n");
            } else {
                qt_hello_serial("[qt] bits\n");
                bpl = img->bytesPerLine() / 4;
                w = img->width();
                h = img->height();
                /* GuestMvpShell layout from DesktopShell.qml. Not W8 gold/navy.
                 * Scale from the 480×320 first hop so D2c 1024×768 still
                 * shows wallpaper / card / EX VW TE / bar. */
                d2_fill(bits, bpl, w, h, 0, 0, w, h, 0xff7a8fa8u);
                d2_fill(bits, bpl, w, h, w * 40 / 480, h * 28 / 320, w * 400 / 480,
                        h * 200 / 320, 0xfff8fafcu);
                d2_fill(bits, bpl, w, h, w * 56 / 480, h * 48 / 320, w * 48 / 480,
                        w * 48 / 480, 0xff1d4ed8u);
                d2_fill(bits, bpl, w, h, w * 120 / 480, h * 48 / 320, w * 48 / 480,
                        w * 48 / 480, 0xff0f766eu);
                d2_fill(bits, bpl, w, h, w * 184 / 480, h * 48 / 320, w * 48 / 480,
                        w * 48 / 480, 0xffc2410cu);
                d2_fill(bits, bpl, w, h, 0, h - (h * 36 / 320), w, h * 36 / 320,
                        0xff334155u);
                qt_hello_serial("[qt] D2c fullscreen\n");
                qt_hello_serial("[qt] D2c ");
                d2_serial_u32((unsigned)w);
                qt_hello_serial("x");
                d2_serial_u32((unsigned)h);
                qt_hello_serial("\n");
                qt_hello_serial("[qt] D2 fill desk\n");
                qt_hello_serial("[qt] fill bits\n");
            }
        }
    }
    store.endPaint();
    qt_hello_serial("[qt] paint\n");
    store.flush(rect);
    qt_hello_serial("[qt] flush\n");

    /* Skip processEvents: Q_EMIT awake / sendPostedEvents hang; vfork waits for exit. */

    qt_hello_serial("[qt] QGuiApplication done\n");
    /* Do not return: QWindow/QBackingStore dtors and Qt atexit hang.
     * C p8test uses exit_group(231). Stack objects are leaked on purpose.
     * getppid: 1 = g_guest_fork_active still set; 0 = vfork slot already gone. */
    {
        register long rax __asm__("rax") = 110;
        register long rdi __asm__("rdi") = 0;
        register long rsi __asm__("rsi") = 0;
        register long rdx __asm__("rdx") = 0;
        register long r10 __asm__("r10") = 0;
        register long r8 __asm__("r8") = 0;
        register long r9 __asm__("r9") = 0;
        __asm__ volatile("syscall"
                         : "+r"(rax)
                         : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        if (rax == 1)
            qt_hello_serial("[qt] ppid=1\n");
        else if (rax == 0)
            qt_hello_serial("[qt] ppid=0\n");
        else
            qt_hello_serial("[qt] ppid=other\n");
    }
    qt_hello_serial("[qt] exit_group\n");
    {
        register long rax __asm__("rax") = 231;
        register long rdi __asm__("rdi") = 0;
        register long rsi __asm__("rsi") = 0;
        register long rdx __asm__("rdx") = 0;
        register long r10 __asm__("r10") = 0;
        register long r8 __asm__("r8") = 0;
        register long r9 __asm__("r9") = 0;
        __asm__ volatile("syscall"
                         : "+r"(rax)
                         : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        if (rax == (long)-4089)
            qt_hello_serial("[qt] exit ret=THREAD_SWITCH\n");
        else if (rax == (long)-4093)
            qt_hello_serial("[qt] exit ret=FORK_PARENT\n");
        else
            qt_hello_serial("[qt] exit returned\n");
        rax = 60;
        rdi = 0;
        __asm__ volatile("syscall"
                         : "+r"(rax)
                         : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        qt_hello_serial("[qt] exit60 returned\n");
        (void)rax;
    }
    for (;;) {
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    qt_hello_serial("[qt] hello hybrid-qpa\n");
    qt_hello_serial("[qt] D1 wayland\n");
    qt_hello_serial("[qt] D2 qml-client\n");
    qt_hello_serial("[qt] D2c fullscreen\n");
    qt_hello_serial("[qt] D2 no-beginCreate\n");
#ifdef BFREE_D2B_QML
    qt_hello_serial("[qt] D2b qml-engine\n");
#endif
    qt_hello_serial("[qt] hello wait-stub\n");
    qt_hello_serial("[qt] QGuiApplication start\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_preflight_musl_heap();
    bfree_guest_preflight_ctor_mmap();
    qt_hello_serial("[qt] enter hybrid QGui session\n");
    bfree_guest_run_on_ctor_stack_hybrid(hello_gui_session);
    qt_hello_serial("[qt] hybrid session returned\n");
    return 0;
}
