#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4object.cpp")
text = path.read_text()
changed = False

decl = 'extern "C" void *bfree_guest_qv4_active_engine(void);\n'
if decl not in text:
    anchor = "using namespace Qt::Literals::StringLiterals;\n\n"
    if anchor in text:
        text = text.replace(anchor, anchor + decl, 1)
    else:
        text = text.replace("#include <stdint.h>\n\n", "#include <stdint.h>\n\n" + decl, 1)
    changed = True

anchor = """void Heap::Object::setUsedAsProto()
{
    internalClass.set(internalClass->engine, internalClass->asProtoClass());
}"""

stub = """void Heap::Object::setUsedAsProto()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    Heap::InternalClass *ic = internalClass.get();
    if (!ic)
        return;
    if (!ic->isUsedAsProto())
        ic->flags |= Heap::InternalClass::UsedAsProto;
#else
    internalClass.set(internalClass->engine, internalClass->asProtoClass());
#endif
}"""

old_guest = """void Heap::Object::setUsedAsProto()
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!internalClass)
        return;
    ExecutionEngine *eng = static_cast<ExecutionEngine *>(bfree_guest_qv4_active_engine());
    if (!eng)
        eng = internalClass->engine;
    if (!eng)
        return;
    internalClass.set(eng, internalClass->asProtoClass());
#else
    internalClass.set(internalClass->engine, internalClass->asProtoClass());
#endif
}"""

if stub in text:
    pass
elif old_guest in text:
    text = text.replace(old_guest, stub, 1)
    changed = True
elif anchor in text:
    text = text.replace(anchor, stub, 1)
    changed = True
else:
    raise SystemExit("qv4object.cpp: setUsedAsProto anchor missing")

if changed:
    path.write_text(text)
    print("[patch_qv4object_guest] ok")
else:
    print("[patch_qv4object_guest] already applied")
PY
