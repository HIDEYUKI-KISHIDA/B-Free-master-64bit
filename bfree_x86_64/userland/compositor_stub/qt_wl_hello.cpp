/* Tiny guest QGuiApplication client. Not desktop.elf, not DesktopShell.qml,
 * not libqbfree.a. Same stack/malloc bring-up as desktop.elf, but MUST
 * return 0 after first-frame flush so g1-desk vfork parent can blit.
 * Do not call bfree_guest_enter_preflighted_mmap_noreturn. */
#include <new>
#include <QBackingStore>
#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
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
    int k;

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
    if (n > 0) {
        const QJsonObject md = QPluginLoader::staticPlugins().at(0).metaData();
        const QJsonArray keys = md.value(QStringLiteral("MetaData")).toObject().value(QStringLiteral("Keys")).toArray();
        k = keys.size();
        qt_hello_serial("[qt] plugin keys=");
        {
            char b[4];
            b[0] = (char)('0' + ((k / 10) % 10));
            b[1] = (char)('0' + (k % 10));
            b[2] = '\n';
            b[3] = 0;
            qt_hello_serial(b);
        }
    }
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

    QWindow win;
    win.setGeometry(0, 0, 480, 320);
    win.setSurfaceType(QSurface::RasterSurface);
    QBackingStore store(&win);
    win.create();
    store.resize(QSize(480, 320));
    win.show();

    const QRect rect(0, 0, 480, 320);
    store.beginPaint(rect);
    {
        QPainter p(store.paintDevice());
        p.fillRect(0, 0, 480, 320, QColor(0x1e, 0x3a, 0x8a));
        p.fillRect(0, 0, 480, 36, QColor(0xd4, 0xa0, 0x17));
        p.fillRect(16, 92, 72, 8, QColor(0x06, 0xb6, 0xd4));
    }
    store.endPaint();
    store.flush(rect);

    for (int i = 0; i < 8; i++) {
        g_app->processEvents();
    }
    qt_hello_serial("[qt] QGuiApplication done\n");
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
