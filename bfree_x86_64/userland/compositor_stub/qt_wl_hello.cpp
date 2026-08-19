/* Tiny guest QGuiApplication client. Not desktop.elf, not DesktopShell.qml,
 * not libqbfree.a. Same stack/malloc bring-up as desktop.elf, but MUST
 * return 0 after first-frame flush so g1-desk vfork parent can blit.
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

extern const QT_PREPEND_NAMESPACE(QStaticPlugin) qt_static_plugin_QBfreeWlIntegrationPlugin();

extern "C" {
void bfree_guest_refresh_libc_auxv(void);
void bfree_guest_preflight_musl_heap(void);
void bfree_guest_preflight_ctor_mmap(void);
void bfree_guest_run_on_ctor_stack_hybrid(void (*fn)(void));
int bfree_guest_ensure_fallback_heap(void);
void bfree_guest_begin_hybrid_alloc(void);
char *getenv(const char *);
}

static char g_prog[] = "/p8test.elf";
static char g_arg_platform[] = "-platform";
static char g_arg_bfree[] = "bfree";
static char *g_qt_argv[] = {g_prog, g_arg_platform, g_arg_bfree, nullptr};
static int g_qt_argc = 3;
static QGuiApplication *g_app;

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

/* wrap_getenv reports QT_QPA_PLATFORM=bfree. Match that key. Return after
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
    }
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

    qt_hello_serial("[qt] window start\n");
    QWindow win;
    qt_hello_serial("[qt] window\n");
    win.setGeometry(0, 0, 480, 320);
    win.setSurfaceType(QSurface::RasterSurface);
    QBackingStore store(&win);
    qt_hello_serial("[qt] store\n");
    win.create();
    qt_hello_serial("[qt] create\n");
    store.resize(QSize(480, 320));
    qt_hello_serial("[qt] resize\n");
    win.show();
    qt_hello_serial("[qt] show\n");

    const QRect rect(0, 0, 480, 320);
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
        int x;
        int y;

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
                for (y = 0; y < h; y++) {
                    unsigned *row = bits + y * bpl;
                    for (x = 0; x < w; x++) {
                        row[x] = 0xff1e3a8au;
                    }
                }
                for (y = 0; y < 36 && y < h; y++) {
                    unsigned *row = bits + y * bpl;
                    for (x = 0; x < w; x++) {
                        row[x] = 0xffd4a017u;
                    }
                }
                for (y = 92; y < 100 && y < h; y++) {
                    unsigned *row = bits + y * bpl;
                    for (x = 16; x < 88 && x < w; x++) {
                        row[x] = 0xff06b6d4u;
                    }
                }
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
     * C p8test uses exit_group(231). Stack objects are leaked on purpose. */
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
    qt_hello_serial("[qt] QGuiApplication start\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_preflight_musl_heap();
    bfree_guest_preflight_ctor_mmap();
    qt_hello_serial("[qt] enter hybrid QGui session\n");
    bfree_guest_run_on_ctor_stack_hybrid(hello_gui_session);
    qt_hello_serial("[qt] hybrid session returned\n");
    return 0;
}
