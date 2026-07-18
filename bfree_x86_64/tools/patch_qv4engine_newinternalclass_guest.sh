#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

newic_old = """Heap::InternalClass *ExecutionEngine::newInternalClass(const VTable *vtable, Object *prototype)
{
    Scope scope(this);
    Scoped<InternalClass> ic(scope, internalClasses(Class_Empty)->changeVTable(vtable));
    return ic->changePrototype(prototype ? prototype->d() : nullptr);
}"""

newic_new = """Heap::InternalClass *ExecutionEngine::newInternalClass(const VTable *vtable, Object *prototype)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *base = internalClasses(Class_Empty);
    if (!base)
        return nullptr;
    Heap::InternalClass *ic = base->changeVTable(vtable);
    return ic ? ic->changePrototype(prototype ? prototype->d() : nullptr) : nullptr;
#else
    Scope scope(this);
    Scoped<InternalClass> ic(scope, internalClasses(Class_Empty)->changeVTable(vtable));
    return ic->changePrototype(prototype ? prototype->d() : nullptr);
#endif
}"""

if newic_new in text:
    print("[patch_newinternalclass] already applied")
elif newic_old in text:
    path.write_text(text.replace(newic_old, newic_new, 1))
    print("[patch_newinternalclass] ok")
else:
    raise SystemExit("newInternalClass anchor missing")

# Guest heap path for String/Symbol class setup
str_old2 = """    bfree_guest_qv4_heartbeat("class_string");
    jsObjects[SymbolProto] = memoryManager->allocate<SymbolPrototype>();
    {
        Heap::InternalClass *symbolClass = classes[Class_Empty]->changeVTable(QV4::Symbol::staticVTable());
        classes[Class_Symbol] = symbolClass ? symbolClass->changePrototype(symbolPrototype()->d()) : nullptr;
    }
    bfree_guest_qv4_heartbeat("class_symbol");"""

str_old = """    ic = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    jsObjects[StringProto] = memoryManager->allocObject<StringPrototype>(ic->d(), /*init =*/ false);
    classes[Class_String] = classes[Class_Empty]->changeVTable(QV4::String::staticVTable())->changePrototype(stringPrototype()->d());
    Q_ASSERT(stringPrototype()->d() && classes[Class_String]->prototype);

    jsObjects[SymbolProto] = memoryManager->allocate<SymbolPrototype>();
    classes[Class_Symbol] = classes[EngineBase::Class_Empty]->changeVTable(QV4::Symbol::staticVTable())->changePrototype(symbolPrototype()->d());"""

str_new = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *stringObjClass = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    jsObjects[StringProto] = memoryManager->allocObject<StringPrototype>(stringObjClass, /*init =*/ false);
    {
        Heap::InternalClass *stringClass = classes[Class_Empty]->changeVTable(QV4::String::staticVTable());
        classes[Class_String] = stringClass ? stringClass->changePrototype(stringPrototype()->d()) : nullptr;
    }
    bfree_guest_qv4_heartbeat("class_string");
    {
        Heap::InternalClass *symbolProtoClass = newInternalClass(QV4::SymbolPrototype::staticVTable(), objectPrototype());
        jsObjects[SymbolProto] = memoryManager->allocObject<SymbolPrototype>(symbolProtoClass, /*init =*/ false);
    }
    bfree_guest_qv4_heartbeat("symbol_proto");
    {
        Heap::InternalClass *symbolClass = classes[Class_Empty]->changeVTable(QV4::Symbol::staticVTable());
        classes[Class_Symbol] = symbolClass ? symbolClass->changePrototype(symbolPrototype()->d()) : nullptr;
    }
    bfree_guest_qv4_heartbeat("class_symbol");
#else
    ic = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    jsObjects[StringProto] = memoryManager->allocObject<StringPrototype>(ic->d(), /*init =*/ false);
    classes[Class_String] = classes[Class_Empty]->changeVTable(QV4::String::staticVTable())->changePrototype(stringPrototype()->d());
    Q_ASSERT(stringPrototype()->d() && classes[Class_String]->prototype);

    jsObjects[SymbolProto] = memoryManager->allocate<SymbolPrototype>();
    classes[Class_Symbol] = classes[EngineBase::Class_Empty]->changeVTable(QV4::Symbol::staticVTable())->changePrototype(symbolPrototype()->d());
#endif"""

text = path.read_text()
if str_new in text:
    print("[patch_newinternalclass] string/symbol guest already applied")
elif str_old2 in text:
    text = text.replace(str_old2, str_new, 1)
    path.write_text(text)
    print("[patch_newinternalclass] ok (upgrade symbol_proto allocObject)")
elif str_old in text:
    text = text.replace(str_old, str_new, 1)
    path.write_text(text)
    print("[patch_newinternalclass] ok (string/symbol guest)")
else:
    raise SystemExit("string/symbol anchor missing")
PY
