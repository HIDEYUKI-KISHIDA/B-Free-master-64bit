#!/usr/bin/env bash
# Fix duplicated BFREE_GUEST string/symbol init blocks in qv4engine.cpp.
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
text = path.read_text()

good = """#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *stringObjClass = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    jsObjects[StringProto] = memoryManager->allocateObject<StringPrototype>(stringObjClass);
    {
        Heap::InternalClass *stringClass = classes[Class_Empty]->changeVTable(QV4::String::staticVTable());
        classes[Class_String] = stringClass ? stringClass->changePrototype(stringPrototype()->d()) : nullptr;
    }
    bfree_guest_qv4_heartbeat("class_string");
    {
        Heap::InternalClass *symbolProtoClass = newInternalClass(QV4::SymbolPrototype::staticVTable(), objectPrototype());
        jsObjects[SymbolProto] = memoryManager->allocateObject<SymbolPrototype>(symbolProtoClass);
    }
    bfree_guest_qv4_heartbeat("symbol_proto");
    {
        Heap::InternalClass *symbolClass = classes[Class_Empty]->changeVTable(QV4::Symbol::staticVTable());
        classes[Class_Symbol] = symbolClass ? symbolClass->changePrototype(symbolPrototype()->d()) : nullptr;
    }
    bfree_guest_qv4_heartbeat("class_symbol");
    bfree_guest_qv4_heartbeat("pre_jsstrings");
    Scope scope(this);
    Scoped<InternalClass> ic(scope);
#else
    ic = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());
    jsObjects[StringProto] = memoryManager->allocObject<StringPrototype>(ic->d(), /*init =*/ false);
    classes[Class_String] = classes[Class_Empty]->changeVTable(QV4::String::staticVTable())->changePrototype(stringPrototype()->d());
    Q_ASSERT(stringPrototype()->d() && classes[Class_String]->prototype);

    jsObjects[SymbolProto] = memoryManager->allocate<SymbolPrototype>();
    classes[Class_Symbol] = classes[EngineBase::Class_Empty]->changeVTable(QV4::Symbol::staticVTable())->changePrototype(symbolPrototype()->d());
#endif"""

start = text.find("#if defined(BFREE_GUEST_FIXED_STACK)\n    Heap::InternalClass *stringObjClass = newInternalClass(QV4::StringObject::staticVTable(), objectPrototype());")
end = text.find("\n    jsStrings[String_Empty]", start)
if start < 0 or end < 0:
    raise SystemExit("[fix_string_block] markers missing")
if text[start:end] == good:
    print("[fix_string_block] already clean")
else:
    path.write_text(text[:start] + good + text[end:])
    print("[fix_string_block] ok")
PY
