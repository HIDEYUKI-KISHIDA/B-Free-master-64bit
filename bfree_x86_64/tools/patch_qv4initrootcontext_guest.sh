#!/usr/bin/env bash
# Guest initRootContext: null-safe alloc + direct activation; explicit globalObject alloc.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

helper = """
#if defined(BFREE_GUEST_FIXED_STACK)
static Heap::ExecutionContext *bfree_guest_root_context_alloc(ExecutionEngine *e)
{
    Heap::ExecutionContext *rd = e->memoryManager->allocManaged<ExecutionContext>();
    if (rd)
        return rd;
    static char g_guestRootCtxPool[8][sizeof(Heap::ExecutionContext)];
    static unsigned g_guestRootCtxSlot;
    const unsigned slot = g_guestRootCtxSlot < 8 ? g_guestRootCtxSlot++ : 7;
    auto *m = reinterpret_cast<Heap::ExecutionContext *>(g_guestRootCtxPool[slot]);
    std::memset(m, 0, sizeof(Heap::ExecutionContext));
    if (Heap::InternalClass *ic = e->internalClasses(EngineBase::Class_ExecutionContext))
        *reinterpret_cast<Heap::InternalClass **>(&m->internalClass) = ic;
    return m;
}
#endif
"""

saved_decl = """
#if defined(BFREE_GUEST_FIXED_STACK)
static Heap::Object *bfree_guest_saved_global_activation;
#endif
"""

if "bfree_guest_saved_global_activation" not in t:
    anchor = 'extern "C" void bfree_guest_qv4_heartbeat(const char *);'
    if anchor not in t:
        raise SystemExit("[initrootctx] heartbeat extern anchor missing")
    t = t.replace(anchor, anchor + saved_decl, 1)
    print("[initrootctx] ok (saved activation decl)")

if "bfree_guest_root_context_alloc" not in t:
    anchor = "void ExecutionEngine::initRootContext()"
    if anchor not in t:
        raise SystemExit("[initrootctx] anchor missing")
    t = t.replace(anchor, helper + anchor, 1)
    print("[initrootctx] ok (helper)")

glob_old2 = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_qv4_heartbeat(globalObject->d() ? "global_obj_ok" : "global_obj_null");
#else"""
glob_new2 = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_saved_global_activation = globalObject ? globalObject->d() : nullptr;
    bfree_guest_qv4_heartbeat(bfree_guest_saved_global_activation ? "global_obj_ok" : "global_obj_null");
#else"""

glob_old = """    *static_cast<Value *>(globalObject) = newObject();
    Q_ASSERT(globalObject->d()->vtable());
    initRootContext();"""

glob_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_saved_global_activation = globalObject ? globalObject->d() : nullptr;
    bfree_guest_qv4_heartbeat(bfree_guest_saved_global_activation ? "global_obj_ok" : "global_obj_null");
#else
    *static_cast<Value *>(globalObject) = newObject();
#endif
    Q_ASSERT(globalObject->d()->vtable());
    initRootContext();"""

if "bfree_guest_saved_global_activation" not in t:
    old_helper_end = "    return m;\n}\n#endif\n"
    new_helper_end = "    return m;\n}\n#endif\n"
    if old_helper_end in t:
        print("[initrootctx] saved activation static already at file scope")
    else:
        raise SystemExit("[initrootctx] saved activation anchor missing")

glob_old3 = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_qv4_heartbeat(globalObject->d() ? "global_obj_ok" : "global_obj_null");
#else"""
glob_new3 = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_saved_global_activation = globalObject ? globalObject->d() : nullptr;
    bfree_guest_qv4_heartbeat(bfree_guest_saved_global_activation ? "global_obj_ok" : "global_obj_null");
#else"""

init_old3 = """    Heap::Object *act = nullptr;
    if (globalObject) {
        const Value *gv = reinterpret_cast<const Value *>(globalObject);
        if (gv->isObject())
            act = gv->objectValue()->d();
        if (!act)
            act = globalObject->d();
    }
    if (!act) {
        bfree_guest_qv4_heartbeat("root_ctx_gobj_null");
        return;
    }
    rd->init(Heap::ExecutionContext::Type_GlobalContext);
    bfree_guest_qv4_heartbeat("root_ctx_inited");
    *reinterpret_cast<Heap::Object **>(&rd->activation) = act;"""

init_new3 = """    Heap::Object *act = bfree_guest_saved_global_activation;
    if (!act && globalObject)
        act = globalObject->d();
    if (!act) {
        bfree_guest_qv4_heartbeat("root_ctx_gobj_null");
        return;
    }
    rd->init(Heap::ExecutionContext::Type_GlobalContext);
    bfree_guest_qv4_heartbeat("root_ctx_inited");
    *reinterpret_cast<Heap::Object **>(&rd->activation) = act;"""

if "bfree_guest_saved_global_activation = globalObject" in t:
    print("[initrootctx] globalObject already patched")
elif glob_old3 in t:
    t = t.replace(glob_old3, glob_new3, 1)
    print("[initrootctx] ok (globalObject v3)")
elif glob_old2 in t:
    t = t.replace(glob_old2, glob_new2, 1)
    print("[initrootctx] ok (globalObject v2)")
elif glob_old in t:
    t = t.replace(glob_old, glob_new, 1)
    print("[initrootctx] ok (globalObject)")
else:
    raise SystemExit("[initrootctx] globalObject anchor missing")

init_old = """void ExecutionEngine::initRootContext()
{
    Scope scope(this);
    Scoped<ExecutionContext> r(scope, memoryManager->allocManaged<ExecutionContext>());
    r->d_unchecked()->init(Heap::ExecutionContext::Type_GlobalContext);
    r->d()->activation.set(this, globalObject->d());
    jsObjects[RootContext] = r;
    jsObjects[ScriptContext] = r;
    jsObjects[IntegerNull] = Encode((int)0);
}"""

init_new = """void ExecutionEngine::initRootContext()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::ExecutionContext *rd = bfree_guest_root_context_alloc(this);
    bfree_guest_qv4_heartbeat(rd ? "root_ctx_alloc" : "root_ctx_alloc_null");
    if (!rd)
        return;
    Heap::Object *act = bfree_guest_saved_global_activation;
    if (!act && globalObject)
        act = globalObject->d();
    if (!act) {
        bfree_guest_qv4_heartbeat("root_ctx_gobj_null");
        return;
    }
    rd->init(Heap::ExecutionContext::Type_GlobalContext);
    bfree_guest_qv4_heartbeat("root_ctx_inited");
    *reinterpret_cast<Heap::Object **>(&rd->activation) = act;
    jsObjects[RootContext] = Value::fromHeapObject(rd);
    jsObjects[ScriptContext] = jsObjects[RootContext];
    jsObjects[IntegerNull] = Encode((int)0);
    bfree_guest_qv4_heartbeat("root_ctx_done");
    return;
#endif
    Scope scope(this);
    Scoped<ExecutionContext> r(scope, memoryManager->allocManaged<ExecutionContext>());
    r->d_unchecked()->init(Heap::ExecutionContext::Type_GlobalContext);
    r->d()->activation.set(this, globalObject->d());
    jsObjects[RootContext] = r;
    jsObjects[ScriptContext] = r;
    jsObjects[IntegerNull] = Encode((int)0);
}"""

init_old2 = """    rd->init(Heap::ExecutionContext::Type_GlobalContext);
    bfree_guest_qv4_heartbeat("root_ctx_inited");
    Heap::Object *act = bfree_guest_saved_global_activation;
    if (!act && globalObject)
        act = globalObject->d();
    if (!act) {
        bfree_guest_qv4_heartbeat("root_ctx_gobj_null");
        return;
    }
    *reinterpret_cast<Heap::Object **>(&rd->activation) = act;"""

init_new2 = """    Heap::Object *act = bfree_guest_saved_global_activation;
    if (!act && globalObject)
        act = globalObject->d();
    if (!act) {
        bfree_guest_qv4_heartbeat("root_ctx_gobj_null");
        return;
    }
    rd->init(Heap::ExecutionContext::Type_GlobalContext);
    bfree_guest_qv4_heartbeat("root_ctx_inited");
    *reinterpret_cast<Heap::Object **>(&rd->activation) = act;"""

if init_old3 in t:
    t = t.replace(init_old3, init_new3, 1)
    print("[initrootctx] ok (initRootContext v3)")
elif init_old2 in t:
    t = t.replace(init_old2, init_new2, 1)
    print("[initrootctx] ok (initRootContext v2)")
elif init_new in t:
    print("[initrootctx] initRootContext already patched")
elif init_old in t:
    t = t.replace(init_old, init_new, 1)
    print("[initrootctx] ok (initRootContext)")
else:
    raise SystemExit("[initrootctx] initRootContext anchor missing")

p.write_text(t)
print("[initrootctx] done")
PY
