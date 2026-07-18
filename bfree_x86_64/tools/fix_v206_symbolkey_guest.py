from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = eng.read_text()

sym_old = """static PropertyKey guestBuiltinSymbolKey(ExecutionEngine *e, int symIndex)
{
    Symbol *s = reinterpret_cast<Symbol *>(e->jsSymbols + symIndex);
    if (s) {
        if (Heap::Symbol *d = s->d())
            return d->identifier;
    }
    return PropertyKey::invalid();
}"""

sym_new = """static PropertyKey guestBuiltinSymbolKey(ExecutionEngine *e, int symIndex)
{
    if (!e || symIndex < 0)
        return PropertyKey::invalid();
    const Value &v = e->jsSymbols[symIndex];
    if (!v.isManaged())
        return PropertyKey::invalid();
    if (Heap::Symbol *d = static_cast<Heap::Symbol *>(v.m())) {
        if (d->identifier.isValid())
            return d->identifier;
        return PropertyKey::fromStringOrSymbol(e, d);
    }
    return PropertyKey::invalid();
}"""

if sym_new not in t:
    if sym_old not in t:
        raise SystemExit("[v206] guestBuiltinSymbolKey anchor missing")
    t = t.replace(sym_old, sym_new, 1)
    print("[v206] guestBuiltinSymbolKey Value slot fix")
else:
    print("[v206] guestBuiltinSymbolKey already patched")

old_block = """    addProtoHasInstance();
    jsObjects[FunctionProto] = memoryManager->allocObject<FunctionPrototype>(ic->d());
    ic = newInternalClass(FunctionObject::staticVTable(), functionPrototype());"""

new_block = """    addProtoHasInstance();
#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat(ic ? "func_proto_ic_ok" : "func_proto_ic_null");
    if (ic) {
        if (auto *fp = memoryManager->allocateObject<FunctionPrototype>(ic->d())) {
            jsObjects[FunctionProto] = fp;
            bfree_guest_qv4_heartbeat("func_proto_alloc");
        } else {
            bfree_guest_qv4_heartbeat("func_proto_alloc_null");
        }
    }
#else
    jsObjects[FunctionProto] = memoryManager->allocObject<FunctionPrototype>(ic->d());
#endif
    ic = newInternalClass(FunctionObject::staticVTable(), functionPrototype());"""

if "func_proto_alloc" not in t:
    if old_block not in t:
        raise SystemExit("[v206] FunctionProto alloc anchor missing")
    t = t.replace(old_block, new_block, 1)
    print("[v206] FunctionProto guest allocateObject")
else:
    print("[v206] FunctionProto block already patched")

eng.write_text(t)
print("[v206] done")
