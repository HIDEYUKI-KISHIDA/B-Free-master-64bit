/* Guest QPA for the compositor stub. Key is "wayland" (and "bfreewl").
 * Speaks the stub dialect (canned object ids + /tmp/wlXX tiles), not
 * upstream qtwayland. Do not link libqbfree.a / guest_link_compat bfree env
 * as the selected platform — argv is -platform wayland. */
#define QT_STATICPLUGIN 1

#include "wl_stub_client.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QtPlugin>

#include <qpa/qplatformbackingstore.h>
#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformintegrationplugin.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>

QT_BEGIN_NAMESPACE

static void qt_wl_serial(const char *s)
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

class QBfreeWlScreen : public QPlatformScreen
{
public:
    QRect geometry() const override { return QRect(0, 0, 1024, 768); }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
};

class QBfreeWlWindow : public QPlatformWindow
{
public:
    explicit QBfreeWlWindow(QWindow *window) : QPlatformWindow(window)
    {
        QPlatformWindow::requestActivateWindow();
    }

    void setVisible(bool visible) override
    {
        QPlatformWindow::setVisible(visible);
        if (visible) {
            const QSize sz = window()->size();
            QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(0, 0), sz));
        }
    }
};

class QBfreeWlBackingStore : public QPlatformBackingStore
{
public:
    explicit QBfreeWlBackingStore(QWindow *window) : QPlatformBackingStore(window) {}

    QPaintDevice *paintDevice() override { return &m_image; }

    void resize(const QSize &size, const QRegion &) override
    {
        if (m_image.size() != size) {
            m_image = QImage(size, QImage::Format_RGB32);
        }
    }

    void flush(QWindow *, const QRegion &, const QPoint &) override
    {
        if (m_image.isNull()) {
            return;
        }
        QImage img = m_image;
        if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32
            && img.format() != QImage::Format_ARGB32_Premultiplied) {
            img = img.convertToFormat(QImage::Format_RGB32);
        }
        const unsigned w = (unsigned)img.width();
        const unsigned h = (unsigned)img.height();
        qt_wl_serial("[qt] QGuiApplication flush\n");
        (void)wl_stub_flush_app(img.constBits(), w, h);
    }

private:
    QImage m_image;
};

class DummyFontDatabase : public QPlatformFontDatabase
{
public:
    void populateFontDatabase() override {}
};

/* Guest libstdc++ is threads=no; Unix epoll dispatcher is not linked.
 * First-frame hello calls processEvents a few times then exits so vfork
 * parent can blit. Do not call createUnixEventDispatcher(). */
class QBfreeWlEventDispatcher : public QAbstractEventDispatcher
{
public:
    bool processEvents(QEventLoop::ProcessEventsFlags flags) override
    {
        Q_EMIT awake();
        QCoreApplication::sendPostedEvents();
        return QWindowSystemInterface::sendWindowSystemEvents(flags);
    }

    void registerSocketNotifier(QSocketNotifier *notifier) override { (void)notifier; }
    void unregisterSocketNotifier(QSocketNotifier *notifier) override { (void)notifier; }

    void registerTimer(int timerId, qint64 interval, Qt::TimerType timerType,
                       QObject *object) override
    {
        (void)timerId;
        (void)interval;
        (void)timerType;
        (void)object;
    }
    bool unregisterTimer(int timerId) override
    {
        (void)timerId;
        return false;
    }
    bool unregisterTimers(QObject *object) override
    {
        (void)object;
        return false;
    }
    QList<TimerInfo> registeredTimers(QObject *object) const override
    {
        (void)object;
        return {};
    }
    int remainingTime(int timerId) override
    {
        (void)timerId;
        return -1;
    }

    void wakeUp() override {}
    void interrupt() override {}
};

class QBfreeWlIntegration : public QPlatformIntegration
{
public:
    QBfreeWlIntegration()
        : m_fontDatabase(nullptr)
        , m_screen(new QBfreeWlScreen)
    {
        QWindowSystemInterface::handleScreenAdded(m_screen);
    }

    ~QBfreeWlIntegration() override
    {
        QWindowSystemInterface::handleScreenRemoved(m_screen);
        delete m_fontDatabase;
    }

    bool hasCapability(QPlatformIntegration::Capability cap) const override
    {
        switch (cap) {
        case ThreadedPixmaps:
        case MultipleWindows:
            return true;
        case RhiBasedRendering:
        case OpenGL:
        case ThreadedOpenGL:
            return false;
        default:
            return QPlatformIntegration::hasCapability(cap);
        }
    }

    QPlatformWindow *createPlatformWindow(QWindow *window) const override
    {
        return new QBfreeWlWindow(window);
    }

    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override
    {
        return new QBfreeWlBackingStore(window);
    }

    QAbstractEventDispatcher *createEventDispatcher() const override
    {
        qt_wl_serial("[qt] QPA dispatcher\n");
        return new QBfreeWlEventDispatcher;
    }

    QPlatformFontDatabase *fontDatabase() const override
    {
        if (!m_fontDatabase) {
            m_fontDatabase = new DummyFontDatabase;
        }
        return m_fontDatabase;
    }

    QPlatformNativeInterface *nativeInterface() const override
    {
        if (!m_nativeInterface) {
            m_nativeInterface = new QPlatformNativeInterface;
        }
        return m_nativeInterface;
    }

private:
    mutable QPlatformFontDatabase *m_fontDatabase;
    mutable QPlatformNativeInterface *m_nativeInterface = nullptr;
    QBfreeWlScreen *m_screen;
};

class QBfreeWlIntegrationPlugin : public QPlatformIntegrationPlugin
{
public:
    QPlatformIntegration *create(const QString &system, const QStringList &) override
    {
        /* Keys parse may still fail; once instance() runs, accept any QPA name. */
        (void)system;
        qt_wl_serial("[qt] QPA wayland create\n");
        return new QBfreeWlIntegration;
    }
};

static QObject *qt_plugin_instance_QBfreeWlIntegrationPlugin()
{
    qt_wl_serial("[qt] plugin instance\n");
    static QBfreeWlIntegrationPlugin *inst = new QBfreeWlIntegrationPlugin;
    return inst;
}

/* Qt 6.8 static plugin: 4-byte Header then CBOR (IID=2, className=3, MetaData=4).
 * QFactoryLoader slices sizeof(Header) then parses CBOR. Do not use
 * QPluginMetaDataV2 with an array NTTP — gcc may decay it to a pointer and
 * store only 8 bytes of payload, so Keys never match and create() is skipped. */
static constexpr unsigned char qt_pluginMetaDataCbor[] = {
    0xa3, 0x02, 0x78, 0x3e, 0x6f, 0x72, 0x67, 0x2e, 0x71, 0x74, 0x2d, 0x70,
    0x72, 0x6f, 0x6a, 0x65, 0x63, 0x74, 0x2e, 0x51, 0x74, 0x2e, 0x51, 0x50,
    0x41, 0x2e, 0x51, 0x50, 0x6c, 0x61, 0x74, 0x66, 0x6f, 0x72, 0x6d, 0x49,
    0x6e, 0x74, 0x65, 0x67, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x46, 0x61,
    0x63, 0x74, 0x6f, 0x72, 0x79, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61,
    0x63, 0x65, 0x2e, 0x35, 0x2e, 0x33, 0x03, 0x78, 0x19, 0x51, 0x42, 0x66,
    0x72, 0x65, 0x65, 0x57, 0x6c, 0x49, 0x6e, 0x74, 0x65, 0x67, 0x72, 0x61,
    0x74, 0x69, 0x6f, 0x6e, 0x50, 0x6c, 0x75, 0x67, 0x69, 0x6e, 0x04, 0xa1,
    0x64, 0x4b, 0x65, 0x79, 0x73, 0x86, 0x67, 0x77, 0x61, 0x79, 0x6c, 0x61,
    0x6e, 0x64, 0x67, 0x62, 0x66, 0x72, 0x65, 0x65, 0x77, 0x6c, 0x65, 0x62,
    0x66, 0x72, 0x65, 0x65, 0x69, 0x6f, 0x66, 0x66, 0x73, 0x63, 0x72, 0x65,
    0x65, 0x6e, 0x67, 0x6d, 0x69, 0x6e, 0x69, 0x6d, 0x61, 0x6c, 0x63, 0x78,
    0x63, 0x62
};

static void qt_wl_hex8(const unsigned char *p)
{
    static const char hex[] = "0123456789abcdef";
    char b[18];
    int i;
    for (i = 0; i < 8; i++) {
        b[i * 2] = hex[p[i] >> 4];
        b[i * 2 + 1] = hex[p[i] & 15];
    }
    b[16] = '\n';
    b[17] = 0;
    qt_wl_serial("[qt] plugin hdr=");
    qt_wl_serial(b);
}

static QPluginMetaData qt_plugin_query_metadata_QBfreeWlIntegrationPlugin()
{
    static unsigned char raw[4 + sizeof(qt_pluginMetaDataCbor)];
    static int once;
    if (!once) {
        unsigned i;
        raw[0] = 1;
        raw[1] = (unsigned char)QT_VERSION_MAJOR;
        raw[2] = (unsigned char)QT_VERSION_MINOR;
#ifdef QT_NO_DEBUG
        raw[3] = 1;
#else
        raw[3] = (unsigned char)(1u | 0x80u);
#endif
        for (i = 0; i < sizeof(qt_pluginMetaDataCbor); i++) {
            raw[4 + i] = qt_pluginMetaDataCbor[i];
        }
        once = 1;
        qt_wl_hex8(raw);
    }
    return {raw, sizeof(raw)};
}

const QStaticPlugin qt_static_plugin_QBfreeWlIntegrationPlugin()
{
    return {qt_plugin_instance_QBfreeWlIntegrationPlugin,
            qt_plugin_query_metadata_QBfreeWlIntegrationPlugin};
}

QT_END_NAMESPACE
