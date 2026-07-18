from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

old = """    bfree_guest_qv4_heartbeat(jsObjects[Date_Ctor].isManaged() ? "date_ctor_ok" : "date_ctor_null");
    if (auto *o = memoryManager->allocate<RegExpCtor>(this))
        jsObjects[RegExp_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[RegExp_Ctor].isManaged() ? "regexp_ctor_ok" : "regexp_ctor_null");
    if (auto *o = memoryManager->allocate<ErrorCtor>(this))
        jsObjects[Error_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Error_Ctor].isManaged() ? "error_ctor_ok" : "error_ctor_null");
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

new = """    bfree_guest_qv4_heartbeat(jsObjects[Date_Ctor].isManaged() ? "date_ctor_ok" : "date_ctor_null");
    bfree_guest_qv4_heartbeat("regexp_ctor_skip");
    bfree_guest_qv4_heartbeat("error_ctors_skip");
    bfree_guest_qv4_heartbeat("builtin_ctors_done");"""

if "regexp_ctor_skip" in te:
    print("[v223] regexp/error ctor skip already patched")
elif old not in te:
    raise SystemExit("[v223] ctors anchor missing")
else:
    te = te.replace(old, new, 1)
    eng.write_text(te)
    print("[v223] regexp/error ctor skip")

print("[v223] done")
