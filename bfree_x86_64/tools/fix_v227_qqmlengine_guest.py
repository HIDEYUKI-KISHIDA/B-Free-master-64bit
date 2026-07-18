from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlengine.cpp")
te = eng.read_text()

if "qqml_init_enter" in te:
    print("[v227] qqmlengine guest patch already applied")
    raise SystemExit(0)

extern = """#if defined(BFREE_GUEST_FIXED_STACK)
extern "C" void bfree_guest_qv4_heartbeat(const char *);
#endif

"""
if 'extern "C" void bfree_guest_qv4_heartbeat' not in te:
    te = te.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + extern, 1)

init_old = """void QQmlEnginePrivate::init()
{
    Q_Q(QQmlEngine);

    if (baseModulesUninitialized) {
        // Register builtins
        qml_register_types_QML();

        // No need to specifically register those.
        static_assert(std::is_same_v<QStringList, QList<QString>>);
        static_assert(std::is_same_v<QVariantList, QList<QVariant>>);

        qRegisterMetaType<QQmlScriptString>();
        qRegisterMetaType<QQmlComponent::Status>();
        qRegisterMetaType<QList<QObject*> >();
        qRegisterMetaType<QQmlBinding*>();

        // Protect the module: We don't want any URL interceptor to mess with the builtins.
        qmlProtectModule("QML", 1);

        QQmlData::init();
        baseModulesUninitialized = false;
    }

    q->handle()->setQmlEngine(q);

    rootContext = new QQmlContext(q,true);
}"""

init_new = """void QQmlEnginePrivate::init()
{
    Q_Q(QQmlEngine);

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("qqml_init_enter");
#endif

    if (baseModulesUninitialized) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_register_skip");
#else
        // Register builtins
        qml_register_types_QML();
#endif

        // No need to specifically register those.
        static_assert(std::is_same_v<QStringList, QList<QString>>);
        static_assert(std::is_same_v<QVariantList, QList<QVariant>>);

#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_metatypes_enter");
#endif
        qRegisterMetaType<QQmlScriptString>();
        qRegisterMetaType<QQmlComponent::Status>();
        qRegisterMetaType<QList<QObject*> >();
        qRegisterMetaType<QQmlBinding*>();

#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_metatypes_done");
        bfree_guest_qv4_heartbeat("qqml_protect_skip");
#else
        // Protect the module: We don't want any URL interceptor to mess with the builtins.
        qmlProtectModule("QML", 1);
#endif

#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_data_init_enter");
#endif
        QQmlData::init();
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("qqml_data_init_done");
#endif
        baseModulesUninitialized = false;
    }

#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("qqml_set_engine_enter");
#endif
    q->handle()->setQmlEngine(q);
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("qqml_set_engine_done");
    bfree_guest_qv4_heartbeat("qqml_root_ctx_enter");
#endif

    rootContext = new QQmlContext(q,true);
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("qqml_root_ctx_done");
    bfree_guest_qv4_heartbeat("qqml_init_done");
#endif
}"""

ctor_old = """QQmlEngine::QQmlEngine(QObject *parent)
: QJSEngine(*new QQmlEnginePrivate(this), parent)
{
    Q_D(QQmlEngine);
    d->init();
    QJSEnginePrivate::addToDebugServer(this);
}"""

ctor_new = """QQmlEngine::QQmlEngine(QObject *parent)
: QJSEngine(*new QQmlEnginePrivate(this), parent)
{
    Q_D(QQmlEngine);
    d->init();
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("qqml_debug_skip");
#else
    QJSEnginePrivate::addToDebugServer(this);
#endif
}"""

if init_old not in te:
    raise SystemExit("[v227] init() anchor missing")
if ctor_old not in te:
    raise SystemExit("[v227] QQmlEngine ctor anchor missing")

te = te.replace(init_old, init_new, 1)
te = te.replace(ctor_old, ctor_new, 1)
eng.write_text(te)
print("[v227] qqmlengine guest heartbeats + debug skip")
