from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

if "qml_global_skip" in te:
    print("[v228] setQmlEngine guest skip already patched")
    raise SystemExit(0)

old = """void ExecutionEngine::setQmlEngine(QQmlEngine *engine)
{
    // Second stage of initialization. We're updating some more prototypes here.
    isInitialized = false;
    m_qmlEngine = engine;
    initQmlGlobalObject();
    isInitialized = true;
}"""

new = """void ExecutionEngine::setQmlEngine(QQmlEngine *engine)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("set_qml_engine_enter");
    isInitialized = false;
    m_qmlEngine = engine;
    bfree_guest_qv4_heartbeat("qml_global_skip");
    isInitialized = true;
    bfree_guest_qv4_heartbeat("set_qml_engine_done");
    return;
#endif
    // Second stage of initialization. We're updating some more prototypes here.
    isInitialized = false;
    m_qmlEngine = engine;
    initQmlGlobalObject();
    isInitialized = true;
}"""

if old not in te:
    raise SystemExit("[v228] setQmlEngine anchor missing")
eng.write_text(te.replace(old, new, 1))
print("[v228] setQmlEngine guest skip initQmlGlobalObject")
