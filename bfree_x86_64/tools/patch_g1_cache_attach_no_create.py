#!/usr/bin/env python3
"""Attach via ExecutionEngine::executableCompilationUnit (public).

ExecutableCompilationUnit::create is private. Do not call it.
Do not call create(cu, v4engine()).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

NULLENGINE_RE = re.compile(
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache exec create nullengine begin\\n\"\);\r?\n"
    r"[ \t]*QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =\r?\n"
    r"[ \t]*QV4::ExecutableCompilationUnit::create\(std::move\(cu\), nullptr\);\r?\n"
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache exec create nullengine ok\\n\"\);\r?\n"
)

ENGINE = r'''    guest_serial_puts("[desktop_qt] G1 cache exec engine begin\n");
    QQmlEnginePrivate *ep = QQmlEnginePrivate::get(g_engine);
    if (!ep || !ep->v4engine()) {
        guest_serial_puts("[desktop_qt] G1 cache exec v4 null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache exec engine call\n");
    QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =
        ep->v4engine()->executableCompilationUnit(std::move(cu));
    guest_serial_puts("[desktop_qt] G1 cache exec engine ok\n");
'''


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    if "executableCompilationUnit(std::move(cu))" in src:
        print("[ok] already engine wrap", path)
        return
    if "ExecutableCompilationUnit::create" in src and not NULLENGINE_RE.search(src):
        raise SystemExit("private create() still present; refuse v4engine create")
    if NULLENGINE_RE.search(src):
        src = NULLENGINE_RE.sub(lambda _m: ENGINE, src, count=1)
        path.write_text(src, encoding="utf-8")
        print("[ok] engine wrap", path)
        return
    raise SystemExit("nullengine site not found")


if __name__ == "__main__":
    main()
