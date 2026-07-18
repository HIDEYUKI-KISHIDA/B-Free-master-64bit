from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

ctors_old = """    if (auto *o = memoryManager->allocate<ArrayCtor>(this))
        jsObjects[Array_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Array_Ctor].isManaged() ? "array_ctor_ok" : "array_ctor_null");
    if (auto *o = memoryManager->allocate<FunctionCtor>(this))
        jsObjects[Function_Ctor] = o;
    if (auto *o = memoryManager->allocate<GeneratorFunctionCtor>(this))
        jsObjects[GeneratorFunction_Ctor] = o;
    if (auto *o = memoryManager->allocate<DateCtor>(this))
        jsObjects[Date_Ctor] = o;
    if (auto *o = memoryManager->allocate<RegExpCtor>(this))
        jsObjects[RegExp_Ctor] = o;
    if (auto *o = memoryManager->allocate<ErrorCtor>(this))
        jsObjects[Error_Ctor] = o;
    if (auto *o = memoryManager->allocate<EvalErrorCtor>(this))
        jsObjects[EvalError_Ctor] = o;
    if (auto *o = memoryManager->allocate<RangeErrorCtor>(this))
        jsObjects[RangeError_Ctor] = o;
    if (auto *o = memoryManager->allocate<ReferenceErrorCtor>(this))
        jsObjects[ReferenceError_Ctor] = o;
    if (auto *o = memoryManager->allocate<SyntaxErrorCtor>(this))
        jsObjects[SyntaxError_Ctor] = o;
    if (auto *o = memoryManager->allocate<TypeErrorCtor>(this))
        jsObjects[TypeError_Ctor] = o;
    if (auto *o = memoryManager->allocate<URIErrorCtor>(this))
        jsObjects[URIError_Ctor] = o;
    bfree_guest_qv4_heartbeat("builtin_ctors_done");"""

ctors_new = """    if (auto *o = memoryManager->allocate<ArrayCtor>(this))
        jsObjects[Array_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Array_Ctor].isManaged() ? "array_ctor_ok" : "array_ctor_null");
    bfree_guest_qv4_heartbeat("function_ctor_skip");
    bfree_guest_qv4_heartbeat("generator_ctor_skip");
    if (auto *o = memoryManager->allocate<DateCtor>(this))
        jsObjects[Date_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Date_Ctor].isManaged() ? "date_ctor_ok" : "date_ctor_null");
    bfree_guest_qv4_heartbeat("regexp_ctor_skip");
    bfree_guest_qv4_heartbeat("error_ctors_skip");
    bfree_guest_qv4_heartbeat("builtin_ctors_done");"""

if "function_ctor_ok" in te or "function_ctor_skip" in te:
    print("[v222] per-ctor heartbeats already patched")
elif ctors_old not in te:
    raise SystemExit("[v222] ctors anchor missing")
else:
    te = te.replace(ctors_old, ctors_new, 1)
    eng.write_text(te)
    print("[v222] per-ctor heartbeats")

print("[v222] done")
