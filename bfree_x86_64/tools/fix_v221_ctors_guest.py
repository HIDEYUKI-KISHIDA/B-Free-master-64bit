from pathlib import Path

eng = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
te = eng.read_text()

proto_old = """    if (objectClass) {
        bfree_guest_qv4_heartbeat("object_proto_pre");
        jsObjects[ObjectProto] = memoryManager->allocObject<ObjectPrototype>(objectClass);
        bfree_guest_qv4_heartbeat(jsObjects[ObjectProto].isManaged() ? "object_proto_ok" : "object_proto_null");
    } else {
        bfree_guest_qv4_heartbeat("object_proto_no_ic");
    }
    Heap::InternalClass *classObject = nullptr;
    if (objectClass && jsObjects[ObjectProto].isManaged()) {
        Heap::Object *opHeap = static_cast<Heap::Object *>(jsObjects[ObjectProto].m());
        if (opHeap) {
            opHeap->setUsedAsProto();
            classObject = objectClass->changePrototype(opHeap);
        }
    }"""

proto_new = """    Heap::Object *opHeap = nullptr;
    if (objectClass) {
        bfree_guest_qv4_heartbeat("object_proto_pre");
        opHeap = memoryManager->allocObject<ObjectPrototype>(objectClass);
        if (opHeap)
            jsObjects[ObjectProto] = opHeap;
        bfree_guest_qv4_heartbeat(opHeap ? "object_proto_ok" : "object_proto_null");
    } else {
        bfree_guest_qv4_heartbeat("object_proto_no_ic");
    }
    Heap::InternalClass *classObject = nullptr;
    if (objectClass && opHeap) {
        opHeap->setUsedAsProto();
        classObject = objectClass->changePrototype(opHeap);
    }"""

ctors_old = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("object_ctor_enter");
#endif
    jsObjects[Object_Ctor] = memoryManager->allocate<ObjectCtor>(this);
    jsObjects[String_Ctor] = memoryManager->allocate<StringCtor>(this);
    jsObjects[Symbol_Ctor] = memoryManager->allocate<SymbolCtor>(this);
    jsObjects[Number_Ctor] = memoryManager->allocate<NumberCtor>(this);
    jsObjects[Boolean_Ctor] = memoryManager->allocate<BooleanCtor>(this);
    jsObjects[Array_Ctor] = memoryManager->allocate<ArrayCtor>(this);
    jsObjects[Function_Ctor] = memoryManager->allocate<FunctionCtor>(this);
    jsObjects[GeneratorFunction_Ctor] = memoryManager->allocate<GeneratorFunctionCtor>(this);
    jsObjects[Date_Ctor] = memoryManager->allocate<DateCtor>(this);
    jsObjects[RegExp_Ctor] = memoryManager->allocate<RegExpCtor>(this);
    jsObjects[Error_Ctor] = memoryManager->allocate<ErrorCtor>(this);
    jsObjects[EvalError_Ctor] = memoryManager->allocate<EvalErrorCtor>(this);
    jsObjects[RangeError_Ctor] = memoryManager->allocate<RangeErrorCtor>(this);
    jsObjects[ReferenceError_Ctor] = memoryManager->allocate<ReferenceErrorCtor>(this);
    jsObjects[SyntaxError_Ctor] = memoryManager->allocate<SyntaxErrorCtor>(this);
    jsObjects[TypeError_Ctor] = memoryManager->allocate<TypeErrorCtor>(this);
    jsObjects[URIError_Ctor] = memoryManager->allocate<URIErrorCtor>(this);
    jsObjects[IteratorProto] = memoryManager->allocate<IteratorPrototype>();"""

ctors_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    bfree_guest_qv4_heartbeat("object_ctor_enter");
    if (auto *o = memoryManager->allocate<ObjectCtor>(this))
        jsObjects[Object_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[Object_Ctor].isManaged() ? "object_ctor_ok" : "object_ctor_null");
    if (auto *o = memoryManager->allocate<StringCtor>(this))
        jsObjects[String_Ctor] = o;
    bfree_guest_qv4_heartbeat(jsObjects[String_Ctor].isManaged() ? "string_ctor_ok" : "string_ctor_null");
    if (auto *o = memoryManager->allocate<SymbolCtor>(this))
        jsObjects[Symbol_Ctor] = o;
    if (auto *o = memoryManager->allocate<NumberCtor>(this))
        jsObjects[Number_Ctor] = o;
    if (auto *o = memoryManager->allocate<BooleanCtor>(this))
        jsObjects[Boolean_Ctor] = o;
    if (auto *o = memoryManager->allocate<ArrayCtor>(this))
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
    bfree_guest_qv4_heartbeat("builtin_ctors_done");
    if (auto *o = memoryManager->allocate<IteratorPrototype>())
        jsObjects[IteratorProto] = o;
    bfree_guest_qv4_heartbeat(jsObjects[IteratorProto].isManaged() ? "iterator_proto_ok" : "iterator_proto_null");
#else
    jsObjects[Object_Ctor] = memoryManager->allocate<ObjectCtor>(this);
    jsObjects[String_Ctor] = memoryManager->allocate<StringCtor>(this);
    jsObjects[Symbol_Ctor] = memoryManager->allocate<SymbolCtor>(this);
    jsObjects[Number_Ctor] = memoryManager->allocate<NumberCtor>(this);
    jsObjects[Boolean_Ctor] = memoryManager->allocate<BooleanCtor>(this);
    jsObjects[Array_Ctor] = memoryManager->allocate<ArrayCtor>(this);
    jsObjects[Function_Ctor] = memoryManager->allocate<FunctionCtor>(this);
    jsObjects[GeneratorFunction_Ctor] = memoryManager->allocate<GeneratorFunctionCtor>(this);
    jsObjects[Date_Ctor] = memoryManager->allocate<DateCtor>(this);
    jsObjects[RegExp_Ctor] = memoryManager->allocate<RegExpCtor>(this);
    jsObjects[Error_Ctor] = memoryManager->allocate<ErrorCtor>(this);
    jsObjects[EvalError_Ctor] = memoryManager->allocate<EvalErrorCtor>(this);
    jsObjects[RangeError_Ctor] = memoryManager->allocate<RangeErrorCtor>(this);
    jsObjects[ReferenceError_Ctor] = memoryManager->allocate<ReferenceErrorCtor>(this);
    jsObjects[SyntaxError_Ctor] = memoryManager->allocate<SyntaxErrorCtor>(this);
    jsObjects[TypeError_Ctor] = memoryManager->allocate<TypeErrorCtor>(this);
    jsObjects[URIError_Ctor] = memoryManager->allocate<URIErrorCtor>(this);
    jsObjects[IteratorProto] = memoryManager->allocate<IteratorPrototype>();
#endif"""

fn = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4functionobject.cpp")
ft = fn.read_text()
fn_old = """void Heap::FunctionObject::init(QV4::ExecutionEngine *engine, const QString &name)
{
    Scope valueScope(engine);
    ScopedString s(valueScope, engine->newString(name));
    init(engine, s);
}"""
fn_new = """void Heap::FunctionObject::init(QV4::ExecutionEngine *engine, const QString &name)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Object::init();
    Q_UNUSED(engine);
    Q_UNUSED(name);
#else
    Scope valueScope(engine);
    ScopedString s(valueScope, engine->newString(name));
    init(engine, s);
#endif
}"""

mm = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm_p.h")
mt = mm.read_text()
mm_old = """        QV4::Object *proto = ObjectType::defaultPrototype(engine);
        ic = ic->changePrototype(proto ? proto->d() : nullptr);
        if (!ic)
            return nullptr;
        return allocateObject<ObjectType>(ic);"""
mm_new = """        QV4::Object *proto = ObjectType::defaultPrototype(engine);
        Heap::Object *protoHeap = nullptr;
        if (proto) {
            const Value *pv = reinterpret_cast<const Value *>(proto);
            if (pv->isManaged())
                protoHeap = static_cast<Heap::Object *>(pv->m());
        }
        ic = ic->changePrototype(protoHeap);
        if (!ic)
            return nullptr;
        return allocateObject<ObjectType>(ic);"""

changed = False
if proto_new.split("opHeap = memoryManager")[0] in te:
    print("[v221] class_object direct opHeap already patched")
elif proto_old not in te:
    raise SystemExit("[v221] class_object anchor missing")
else:
    te = te.replace(proto_old, proto_new, 1)
    changed = True
    print("[v221] class_object direct opHeap")

if "object_ctor_ok" in te:
    print("[v221] ctor guest guards already patched")
elif ctors_old not in te:
    raise SystemExit("[v221] ctors anchor missing")
else:
    te = te.replace(ctors_old, ctors_new, 1)
    changed = True
    print("[v221] ctor guest guards + heartbeats")

if changed:
    eng.write_text(te)

if fn_new.split("Q_UNUSED(name)")[0] in ft:
    print("[v221] FunctionObject QString init already stubbed")
elif fn_old not in ft:
    raise SystemExit("[v221] FunctionObject init anchor missing")
else:
    fn.write_text(ft.replace(fn_old, fn_new, 1))
    print("[v221] FunctionObject QString init guest stub")

if mm_new.split("protoHeap")[0] in mt:
    print("[v221] allocateObject proto heap already patched")
elif mm_old not in mt:
    raise SystemExit("[v221] allocateObject anchor missing")
else:
    mm.write_text(mt.replace(mm_old, mm_new, 1))
    print("[v221] allocateObject proto heap path")

print("[v221] done")
