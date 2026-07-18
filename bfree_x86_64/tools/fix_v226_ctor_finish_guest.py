from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

setup_old = """    bfree_guest_qv4_heartbeat("global_setup_enter");
    if (globalObject && rootContext() && rootContext()->d()) {
        Heap::Object *gHeap = static_cast<Heap::Object *>(globalObject->d());
        if (gHeap)
            *reinterpret_cast<Heap::Object **>(&rootContext()->d()->activation) = gHeap;
    }
    if (globalObject) {
        if (jsObjects[Object_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Object"), *objectCtor());
        if (jsObjects[String_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("String"), *stringCtor());
        if (jsObjects[Symbol_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Symbol"), *symbolCtor());
        if (jsObjects[Number_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Number"), *numberCtor());
        if (jsObjects[Boolean_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Boolean"), *booleanCtor());
        if (jsObjects[Array_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Array"), *arrayCtor());
        if (jsObjects[Date_Ctor].isManaged())
            globalObject->defineDefaultProperty(QStringLiteral("Date"), *dateCtor());
    }
    bfree_guest_qv4_heartbeat("global_setup_partial");"""

setup_new = """    bfree_guest_qv4_heartbeat("global_setup_enter");
    if (globalObject && rootContext() && rootContext()->d()) {
        Heap::Object *gHeap = nullptr;
        const Value *gv = reinterpret_cast<const Value *>(globalObject);
        if (gv->isManaged())
            gHeap = static_cast<Heap::Object *>(gv->m());
        if (gHeap)
            *reinterpret_cast<Heap::Object **>(&rootContext()->d()->activation) = gHeap;
    }
    bfree_guest_qv4_heartbeat("global_setup_skip");"""

tail_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    FunctionObject *numberObject = jsObjects[Number_Ctor].isManaged() ? numberCtor() : nullptr;
    bfree_guest_qv4_heartbeat("globals_tail_skip");
    QV4::QObjectWrapper::initializeBindings(this);
#else"""

tail_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    FunctionObject *numberObject = nullptr;
    bfree_guest_qv4_heartbeat("globals_tail_skip");
    bfree_guest_qv4_heartbeat("qobj_bindings_skip");
#else"""

ctors_old = """    bfree_guest_qv4_heartbeat(jsObjects[String_Ctor].isManaged() ? "string_ctor_ok" : "string_ctor_null");
    if (auto *o = memoryManager->allocate<SymbolCtor>(this))
        jsObjects[Symbol_Ctor] = o;
    if (auto *o = memoryManager->allocate<NumberCtor>(this))
        jsObjects[Number_Ctor] = o;
    if (auto *o = memoryManager->allocate<BooleanCtor>(this))
        jsObjects[Boolean_Ctor] = o;
    if (auto *o = memoryManager->allocate<ArrayCtor>(this))
        jsObjects[Array_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Array_Ctor].isManaged() ? "array_ctor_ok" : "array_ctor_null");"""

ctors_new = """    bfree_guest_qv4_heartbeat(jsObjects[String_Ctor].isManaged() ? "string_ctor_ok" : "string_ctor_null");
    bfree_guest_qv4_heartbeat("symbol_ctor_skip");
    bfree_guest_qv4_heartbeat("number_ctor_skip");
    bfree_guest_qv4_heartbeat("boolean_ctor_skip");
    if (auto *o = memoryManager->allocate<ArrayCtor>(this))
        jsObjects[Array_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Array_Ctor].isManaged() ? "array_ctor_ok" : "array_ctor_null");"""

changed = False
if "global_setup_skip" in te:
    print("[v226] global setup skip already patched")
elif setup_old not in te:
    raise SystemExit("[v226] global setup anchor missing")
else:
    te = te.replace(setup_old, setup_new, 1)
    changed = True
    print("[v226] global setup skip")

if "qobj_bindings_skip" in te:
    print("[v226] qobj bindings skip already patched")
elif tail_old not in te:
    raise SystemExit("[v226] globals tail anchor missing")
else:
    te = te.replace(tail_old, tail_new, 1)
    changed = True
    print("[v226] qobj bindings skip")

if "symbol_ctor_skip" in te:
    print("[v226] symbol/number/boolean ctor skip already patched")
elif ctors_old not in te:
    raise SystemExit("[v226] ctors anchor missing")
else:
    te = te.replace(ctors_old, ctors_new, 1)
    changed = True
    print("[v226] symbol/number/boolean ctor skip")

if changed:
    eng.write_text(te)
print("[v226] done")
