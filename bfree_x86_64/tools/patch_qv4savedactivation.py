from pathlib import Path

p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

# Remove misplaced decls before namespace
t = t.replace(
    'extern "C" void bfree_guest_qv4_heartbeat(const char *);\n#if defined(BFREE_GUEST_FIXED_STACK)\nstatic Heap::Object *bfree_guest_saved_global_activation;\n#endif\n',
    'extern "C" void bfree_guest_qv4_heartbeat(const char *);\n',
)
t = t.replace(
    "}\nstatic Heap::Object *bfree_guest_saved_global_activation;\n#endif\n\nvoid ExecutionEngine::initRootContext()",
    "}\n#endif\n\nvoid ExecutionEngine::initRootContext()",
)

decl = """
#if defined(BFREE_GUEST_FIXED_STACK)
static Heap::Object *bfree_guest_saved_global_activation;
#endif
"""
anchor = "using namespace QV4;\n"
if "bfree_guest_saved_global_activation" not in t.split("ExecutionEngine::ExecutionEngine")[0]:
    if anchor not in t:
        raise SystemExit("using namespace anchor missing")
    t = t.replace(anchor, anchor + decl, 1)
    print("[fix] added static after using namespace QV4")

old = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_qv4_heartbeat(globalObject->d() ? "global_obj_ok" : "global_obj_null");
#else"""
new = """#if defined(BFREE_GUEST_FIXED_STACK)
    *static_cast<Value *>(globalObject) = newObject();
    bfree_guest_saved_global_activation = globalObject ? globalObject->d() : nullptr;
    bfree_guest_qv4_heartbeat(bfree_guest_saved_global_activation ? "global_obj_ok" : "global_obj_null");
#else"""
if "bfree_guest_saved_global_activation = globalObject" not in t:
    if old not in t:
        raise SystemExit("global object anchor missing")
    t = t.replace(old, new, 1)
    print("[fix] global object save")

p.write_text(t)
print("[fix] done")
