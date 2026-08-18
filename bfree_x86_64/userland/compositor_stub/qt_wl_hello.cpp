/* Tiny guest QGuiApplication client. Not desktop.elf, not DesktopShell.qml,
 * not libqbfree.a. Same stack/malloc bring-up as desktop.elf, but MUST
 * return 0 after first-frame flush so g1-desk vfork parent can blit.
 * Do not call bfree_guest_enter_preflighted_mmap_noreturn. */
#include <QBackingStore>
#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QStaticPlugin>
#include <QWindow>
#include <QtPlugin>

extern const QT_PREPEND_NAMESPACE(QStaticPlugin) qt_static_plugin_QBfreeWlIntegrationPlugin();

extern "C" {
void bfree_guest_refresh_libc_auxv(void);
void bfree_guest_run_on_ctor_stack_plugins(void (*fn)(void));
void bfree_guest_preflight_musl_heap(void);
void bfree_guest_preflight_ctor_mmap(void);
void bfree_guest_run_on_ctor_stack_hybrid(void (*fn)(void));
}

static char g_prog[] = "/p8test.elf";
static char g_arg_platform[] = "-platform";
static char g_arg_bfree[] = "bfree";
static char *g_qt_argv[] = {g_prog, g_arg_platform, g_arg_bfree, nullptr};
static int g_qt_argc = 3;

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

/* qRegister on the exec stack PFs (first Qt heap). desktop.elf uses BSS+bump. */
__attribute__((noinline)) static void hello_register_plugin(void)
{
    qt_hello_serial("[qt] plugin register on ctor stack\n");
    qRegisterStaticPluginFunction(qt_static_plugin_QBfreeWlIntegrationPlugin());
    qt_hello_serial("[qt] plugin registered\n");
}

/* wrap_getenv reports QT_QPA_PLATFORM=bfree. Match that key. Return after
 * flush — vfork waits for child exit, not exec. */
__attribute__((noinline)) static void hello_gui_session(void)
{
    __asm__ volatile("andq $-16, %%rsp" ::: "rsp");
    qt_hello_serial("[qt] before QGuiApplication ctor\n");
    QCoreApplication::setSetuidAllowed(true);
    QGuiApplication::setDesktopSettingsAware(false);
    QGuiApplication app(g_qt_argc, g_qt_argv);
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
        app.processEvents();
    }
    qt_hello_serial("[qt] QGuiApplication done\n");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    qt_hello_serial("[qt] QGuiApplication start\n");
    bfree_guest_refresh_libc_auxv();
    bfree_guest_run_on_ctor_stack_plugins(hello_register_plugin);
    qt_hello_serial("[qt] plugin ctor leave\n");
    bfree_guest_preflight_musl_heap();
    bfree_guest_preflight_ctor_mmap();
    qt_hello_serial("[qt] enter hybrid QGui session\n");
    bfree_guest_run_on_ctor_stack_hybrid(hello_gui_session);
    qt_hello_serial("[qt] hybrid session returned\n");
    return 0;
}
