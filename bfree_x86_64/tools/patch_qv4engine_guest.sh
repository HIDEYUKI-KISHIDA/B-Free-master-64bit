#!/usr/bin/env bash
# Guest QV4: skip env/mutex init + QMetaType converters; add ctor heartbeats.
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

decl = 'extern "C" void bfree_guest_qv4_heartbeat(const char *);\n'
decl2 = 'extern "C" void bfree_guest_qv4_set_active_engine(void *);\n'
if decl not in text:
    text = text.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + decl, 1)
if decl2 not in text:
    text = text.replace("QT_BEGIN_NAMESPACE\n", "QT_BEGIN_NAMESPACE\n" + decl2, 1)

# Move guest stub to top of initializeStaticMembers (skip environmentMutex + getenv).
# Upgrade init_static stub with guest stack sizes.
old_init = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("init_static");
    return;
#endif"""
new_init = """#if defined(BFREE_GUEST_FIXED_STACK)
    s_maxJSStackSize = 2 * 1024 * 1024;
    s_maxGCStackSize = 1 * 1024 * 1024;
    s_jitCallCountThreshold = std::numeric_limits<int>::max();
    bfree_guest_qv4_heartbeat("init_static");
    return;
#endif"""
if new_init in text:
    print("[patch_qv4engine_guest] init_static stack sizes already applied")
elif old_init in text:
    text = text.replace(old_init, new_init, 1)
    print("[patch_qv4engine_guest] ok (init_static stack sizes)")

init_anchor = """void ExecutionEngine::initializeStaticMembers()
{
    bool ok = false;"""

init_stub = """void ExecutionEngine::initializeStaticMembers()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    s_maxJSStackSize = 2 * 1024 * 1024;
    s_maxGCStackSize = 1 * 1024 * 1024;
    s_jitCallCountThreshold = std::numeric_limits<int>::max();
    bfree_guest_qv4_heartbeat("init_static");
    return;
#endif
    bool ok = false;"""

if init_stub in text:
    print("[patch_qv4engine_guest] init_static at top already applied")
elif init_anchor not in text:
    raise SystemExit("qv4engine.cpp: initializeStaticMembers anchor missing")
else:
    text = text.replace(init_anchor, init_stub, 1)
    print("[patch_qv4engine_guest] ok (init_static at top)")

# Remove duplicate mid-function guest stub if present.
dup = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("init_static");
    return;
#endif

    qMetaTypeId<QJSValue>();"""
if dup in text:
    text = text.replace(dup, "    qMetaTypeId<QJSValue>();", 1)
    print("[patch_qv4engine_guest] ok (removed duplicate init_static)")

# Ctor entry heartbeat.
ctor_anchor = """    , m_qmlEngine(nullptr)
{
    if (m_engineId == 1) {"""
ctor_stub = """    , m_qmlEngine(nullptr)
{
    bfree_guest_qv4_set_active_engine(this);
    bfree_guest_qv4_heartbeat("ctor");
    if (m_engineId == 1) {"""

if "bfree_guest_qv4_set_active_engine(this)" not in text:
    if '    bfree_guest_qv4_heartbeat("ctor");\n    if (m_engineId == 1) {' in text:
        text = text.replace(
            '    bfree_guest_qv4_heartbeat("ctor");\n    if (m_engineId == 1) {',
            '    bfree_guest_qv4_set_active_engine(this);\n    bfree_guest_qv4_heartbeat("ctor");\n    if (m_engineId == 1) {',
            1)
        print("[patch_qv4engine_guest] ok (inject set_active_engine)")
    elif ctor_anchor in text:
        text = text.replace(ctor_anchor, ctor_stub, 1)
        print("[patch_qv4engine_guest] ok (ctor set_active_engine)")
    else:
        raise SystemExit("qv4engine.cpp: ctor inject anchor missing")
else:
    print("[patch_qv4engine_guest] set_active_engine already applied")

# Skip yield spin on guest (single engine).
spin_anchor = """    } else if (Q_UNLIKELY(m_engineId & 1)) {
        // This should be rare. You usually don't create lots of engines at the same time.
        while (engineSerial.loadAcquire() & 1) {
            QThread::yieldCurrentThread();
        }
    }"""
spin_stub = """    } else if (Q_UNLIKELY(m_engineId & 1)) {
#if defined(BFREE_GUEST_FIXED_STACK)
        bfree_guest_qv4_heartbeat("engine_wait_skip");
#else
        // This should be rare. You usually don't create lots of engines at the same time.
        while (engineSerial.loadAcquire() & 1) {
            QThread::yieldCurrentThread();
        }
#endif
    }"""
if spin_stub in text:
    print("[patch_qv4engine_guest] engine_wait_skip already applied")
elif spin_anchor in text:
    text = text.replace(spin_anchor, spin_stub, 1)
    print("[patch_qv4engine_guest] ok (skip engine wait spin)")

# Post-init and allocation heartbeats.
steps = [
    ("""    if (m_engineId == 1) {
        initializeStaticMembers();
        engineSerial.storeRelease(2); // make it even
    }""",
     """    if (m_engineId == 1) {
        initializeStaticMembers();
        engineSerial.storeRelease(2); // make it even
        bfree_guest_qv4_heartbeat("post_static");
    }"""),
    ("""    if (s_maxCallDepth < 0) {
        const StackProperties stack = stackProperties();
        cppStackBase = stack.base;
        cppStackLimit = stack.softLimit;
    } else {
        callDepth = 0;
    }

    // We allocate guard pages around our stacks.""",
     """    bfree_guest_qv4_heartbeat("pre_stack");
    if (s_maxCallDepth < 0) {
        const StackProperties stack = stackProperties();
        cppStackBase = stack.base;
        cppStackLimit = stack.softLimit;
    } else {
        callDepth = 0;
    }
    bfree_guest_qv4_heartbeat("post_stack");

    // We allocate guard pages around our stacks."""),
    ("""    memoryManager = new QV4::MemoryManager(this);
    // we don't want to run the gc while the initial setup is not done; not even in aggressive mode
    GCCriticalSection gcCriticalSection(this);""",
     """    memoryManager = new QV4::MemoryManager(this);
    bfree_guest_qv4_heartbeat("mem_mgr");
    // we don't want to run the gc while the initial setup is not done; not even in aggressive mode
    GCCriticalSection gcCriticalSection(this);"""),
    ("""    *jsStack = WTF::PageAllocation::allocate(
                s_maxJSStackSize + 256*1024 + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    jsStackBase = (Value *)jsStack->base();""",
     """    *jsStack = WTF::PageAllocation::allocate(
                s_maxJSStackSize + 256*1024 + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("js_stack");
    jsStackBase = (Value *)jsStack->base();"""),
    ("""    *gcStack = WTF::PageAllocation::allocate(
                s_maxGCStackSize + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);

    exceptionValue = jsAlloca(1);""",
     """    *gcStack = WTF::PageAllocation::allocate(
                s_maxGCStackSize + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("gc_stack");

    exceptionValue = jsAlloca(1);"""),
    ("""    identifierTable = new IdentifierTable(this);

    memset(classes, 0, sizeof(classes));""",
     """    identifierTable = new IdentifierTable(this);
    bfree_guest_qv4_heartbeat("ident");

    memset(classes, 0, sizeof(classes));"""),
    ("""    classes[Class_Empty] = memoryManager->allocIC<InternalClass>();
    classes[Class_Empty]->init(this);

    classes[Class_MemberData]""",
     """    classes[Class_Empty] = memoryManager->allocIC<InternalClass>();
    classes[Class_Empty]->init(this);
    bfree_guest_qv4_heartbeat("class_empty");

    classes[Class_MemberData]"""),
    ("""    classes[Class_Object] = ic->changePrototype(objectPrototype()->d());
    classes[Class_QmlContextWrapper]""",
     """    classes[Class_Object] = ic->changePrototype(objectPrototype()->d());
    bfree_guest_qv4_heartbeat("class_object");
    classes[Class_QmlContextWrapper]"""),
    ("""    *static_cast<Value *>(globalObject) = newObject();
    Q_ASSERT(globalObject->d()->vtable());
    initRootContext();""",
     """    *static_cast<Value *>(globalObject) = newObject();
    Q_ASSERT(globalObject->d()->vtable());
    initRootContext();
    bfree_guest_qv4_heartbeat("root_ctx");"""),
    ("""    m_delayedCallQueue.init(this);
    isInitialized = true;""",
     """    m_delayedCallQueue.init(this);
    bfree_guest_qv4_heartbeat("ctor_done");
    isInitialized = true;"""),
]
for old, new in steps:
    if new in text:
        continue
    if old not in text:
        # noguard guest block may already include these heartbeats
        hb = new.split('bfree_guest_qv4_heartbeat("')[1].split('"')[0] if 'bfree_guest_qv4_heartbeat("' in new else ""
        if hb and f'bfree_guest_qv4_heartbeat("{hb}")' in text:
            print(f"[patch_qv4engine_guest] skip step (hb {hb} already present)")
            continue
        print(f"[patch_qv4engine_guest] warn: step anchor missing (hb={hb or '?'})")
        continue
    text = text.replace(old, new, 1)
    print("[patch_qv4engine_guest] ok (heartbeat step)")

path.write_text(text)
print("[patch_qv4engine_guest] done")

# Guest: disable guard-page remap (MAP_FIXED guard breaks arena on B-Free).
noguard_old = """    const size_t guardPages = 2 * WTF::pageSize();

    memoryManager = new QV4::MemoryManager(this);
    bfree_guest_qv4_heartbeat("mem_mgr");
    // we don't want to run the gc while the initial setup is not done; not even in aggressive mode
    GCCriticalSection gcCriticalSection(this);
    // reserve space for the JS stack
    // we allow it to grow to a bit more than m_maxJSStackSize, as we can overshoot due to ScopedValues
    // allocated outside of JIT'ed methods.
    *jsStack = WTF::PageAllocation::allocate(
                s_maxJSStackSize + 256*1024 + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("js_stack");
    jsStackBase = (Value *)jsStack->base();
#ifdef V4_USE_VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(jsStackBase, m_maxJSStackSize + 256*1024);
#endif

    jsStackTop = jsStackBase;

    *gcStack = WTF::PageAllocation::allocate(
                s_maxGCStackSize + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("gc_stack");"""

noguard_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    memoryManager = new QV4::MemoryManager(this);
    bfree_guest_qv4_heartbeat("mem_mgr");
    GCCriticalSection gcCriticalSection(this);
    *jsStack = WTF::PageAllocation::allocate(
                s_maxJSStackSize + 256 * 1024, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ false);
    bfree_guest_qv4_heartbeat("js_stack");
    jsStackBase = (Value *)jsStack->base();
    jsStackTop = jsStackBase;
    *gcStack = WTF::PageAllocation::allocate(
                s_maxGCStackSize, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ false);
    bfree_guest_qv4_heartbeat("gc_stack");
#else
    const size_t guardPages = 2 * WTF::pageSize();

    memoryManager = new QV4::MemoryManager(this);
    bfree_guest_qv4_heartbeat("mem_mgr");
    // we don't want to run the gc while the initial setup is not done; not even in aggressive mode
    GCCriticalSection gcCriticalSection(this);
    // reserve space for the JS stack
    // we allow it to grow to a bit more than m_maxJSStackSize, as we can overshoot due to ScopedValues
    // allocated outside of JIT'ed methods.
    *jsStack = WTF::PageAllocation::allocate(
                s_maxJSStackSize + 256*1024 + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("js_stack");
    jsStackBase = (Value *)jsStack->base();
#ifdef V4_USE_VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(jsStackBase, m_maxJSStackSize + 256*1024);
#endif

    jsStackTop = jsStackBase;

    *gcStack = WTF::PageAllocation::allocate(
                s_maxGCStackSize + guardPages, WTF::OSAllocator::JSVMStackPages,
                /* writable */ true, /* executable */ false, /* includesGuardPages */ true);
    bfree_guest_qv4_heartbeat("gc_stack");
#endif"""

text = path.read_text()
if noguard_new in text:
    print("[patch_qv4engine_guest] noguard stacks already applied")
elif noguard_old in text:
    text = text.replace(noguard_old, noguard_new, 1)
    path.write_text(text)
    print("[patch_qv4engine_guest] ok (noguard stacks guest)")
else:
    print("[patch_qv4engine_guest] warn: noguard anchor missing")
PY
