#!/usr/bin/env bash
# Guest: skip WriteBarrier::markCustom during ctor (isGCOngoing / markStack path).
set -eu
python3 - <<'PY'
from pathlib import Path
path = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4writebarrier_p.h")
text = path.read_text()

old = """    template<typename F, typename Engine = EngineBase>
    static void markCustom(Engine *engine, F &&markFunction) {
        if (engine->isGCOngoing)
            (std::forward<F>(markFunction))(engine->memoryManager->markStack());
    }"""

new = """    template<typename F, typename Engine = EngineBase>
    static void markCustom(Engine *engine, F &&markFunction) {
#if defined(BFREE_GUEST_FIXED_STACK)
        Q_UNUSED(engine);
        Q_UNUSED(markFunction);
#else
        if (engine->isGCOngoing)
            (std::forward<F>(markFunction))(engine->memoryManager->markStack());
#endif
    }"""

if new in text:
    print("[patch_writebarrier] already applied")
elif old in text:
    path.write_text(text.replace(old, new, 1))
    print("[patch_writebarrier] ok")
else:
    raise SystemExit("[patch_writebarrier] anchor missing")
PY
