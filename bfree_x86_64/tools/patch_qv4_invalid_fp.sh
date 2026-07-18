#!/usr/bin/env bash
# Guard QV4 AOT/JIT calls against poison/low function pointers on B-Free guest.
set -eu
QF=/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4function.cpp
VM=/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4vme_moth.cpp
python3 - <<'PY'
from pathlib import Path

qf = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4function.cpp")
text = qf.read_text()
old = """    if (aotFunction) {
        aotCompiledCode = aotFunction->functionPtr;
        new (&aotCompiledFunction) AOTCompiledFunction;
        kind = AotCompiled;"""
new = """    if (aotFunction && aotFunction->functionPtr
            && reinterpret_cast<quintptr>(aotFunction->functionPtr) > 0x10000u) {
        aotCompiledCode = aotFunction->functionPtr;
        new (&aotCompiledFunction) AOTCompiledFunction;
        kind = AotCompiled;"""
if old not in text:
    raise SystemExit("qv4function.cpp: patch anchor missing")
qf.write_text(text.replace(old, new, 1))

vm = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4vme_moth.cpp")
text = vm.read_text()
old = """    if (function->jittedCode != nullptr && debugger == nullptr) {
        result = function->jittedCode(frame, engine);
    } else {"""
new = """    if (function->jittedCode != nullptr && debugger == nullptr
            && reinterpret_cast<quintptr>(function->jittedCode) > 0x10000u) {
        result = function->jittedCode(frame, engine);
    } else {"""
if old not in text:
    raise SystemExit("qv4vme_moth.cpp: patch anchor missing")
vm.write_text(text.replace(old, new, 1))

text = vm.read_text()
old = """    Function *function = frame->v4Function;
    Q_ASSERT(function->aotCompiledCode);
    Q_TRACE_SCOPE(QQmlV4_function_call, engine, function->name()->toQString(),"""
new = """    Function *function = frame->v4Function;
    if (!function->aotCompiledCode
            || reinterpret_cast<quintptr>(function->aotCompiledCode) <= 0x10000u) {
        frame->setReturnValueUndefined();
        return;
    }
    Q_TRACE_SCOPE(QQmlV4_function_call, engine, function->name()->toQString(),"""
if old not in text:
    raise SystemExit("qv4vme_moth.cpp MetaTypes patch anchor missing")
vm.write_text(text.replace(old, new, 1))
print("[patch_qv4_invalid_fp] ok")
PY
