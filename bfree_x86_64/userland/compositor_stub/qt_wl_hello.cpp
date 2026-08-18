/* Tiny guest QGuiApplication client. Not desktop.elf, not DesktopShell.qml,
 * not bfree QPA. Paints a 480x320 raster window (gold title / navy body /
 * cyan bar) and flushes through the stub Wayland QPA. */
#include <QBackingStore>
#include <QColor>
#include <QGuiApplication>
#include <QPainter>
#include <QStaticPlugin>
#include <QWindow>
#include <QtPlugin>

extern const QT_PREPEND_NAMESPACE(QStaticPlugin) qt_static_plugin_QBfreeWlIntegrationPlugin();

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

int main(int argc, char **argv)
{
    static char prog[] = "/p8test.elf";
    static char arg_platform[] = "-platform";
    static char arg_bfree[] = "bfree";
    static char *qt_argv[] = {prog, arg_platform, arg_bfree, nullptr};
    int qt_argc = 3;

    (void)argc;
    (void)argv;

    qt_hello_serial("[qt] QGuiApplication start\n");
    qRegisterStaticPluginFunction(qt_static_plugin_QBfreeWlIntegrationPlugin());
    qt_hello_serial("[qt] plugin registered\n");
    /* wrap_getenv reports QT_QPA_PLATFORM=bfree. Match that key. */
    qt_hello_serial("[qt] before QGuiApplication ctor\n");
    QGuiApplication::setDesktopSettingsAware(false);
    QGuiApplication app(qt_argc, qt_argv);
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
    return 0;
}
