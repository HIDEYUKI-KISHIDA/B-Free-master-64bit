#!/usr/bin/env python3
"""Attach ExecutableCompilationUnit via create(cu, nullptr).

Ctor is private; exec->data does not exist. Do not call create(cu, v4engine()).
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

NEW_RE = re.compile(
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache exec begin\\n\"\);\r?\n"
    r"[ \t]*QQmlRefPointer<QV4::ExecutableCompilationUnit> exec\(\r?\n"
    r"[ \t]*new QV4::ExecutableCompilationUnit\);\r?\n"
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache exec new ok\\n\"\);\r?\n"
    r"[ \t]*exec->data = cached->qmlData;\r?\n"
    r"[ \t]*exec->aotCompiledFunctions = cached->aotCompiledFunctions;\r?\n"
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache exec data ok\\n\"\);\r?\n"
)

NULLENGINE = r'''    guest_serial_puts("[desktop_qt] G1 cache exec create nullengine begin\n");
    QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =
        QV4::ExecutableCompilationUnit::create(std::move(cu), nullptr);
    guest_serial_puts("[desktop_qt] G1 cache exec create nullengine ok\n");
'''

CU_TAIL_RE = re.compile(
    r"([ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache cu data=\"\);\r?\n"
    r"[ \t]*guest_serial_hex_u64\(\(uint64_t\)\(uintptr_t\)cu->data\);\r?\n"
    r"[ \t]*guest_serial_puts\(\"\\n\"\);\r?\n)"
    r"[ \t]*g_g1_done = 1;\r?\n"
)

ATTACH = NULLENGINE + r'''    QQmlComponentPrivate *priv = QQmlComponentPrivate::get(g_g1_comp);
    if (!priv) {
        guest_serial_puts("[desktop_qt] G1 cache priv null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache priv ok\n");
    priv->compilationUnit = exec;
    guest_serial_puts("[desktop_qt] G1 cache attach ok\n");
    guest_serial_puts("[desktop_qt] G1 IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    guest_serial_puts("\n");
    if (g_g1_comp->isReady())
        guest_serial_puts("[desktop_qt] G1 thin QML Ready\n");
    else if (g_g1_comp->isError())
        guest_serial_puts("[desktop_qt] G1 thin QML error\n");
    else
        guest_serial_puts("[desktop_qt] G1 thin QML not ready\n");
    g_g1_done = 1;
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
    if "create(std::move(cu), nullptr)" in src:
        print("[ok] already nullengine create", path)
        return
    if "v4engine()" in src and "ExecutableCompilationUnit::create" in src:
        raise SystemExit("create(cu, v4engine()) present; refuse")
    if NEW_RE.search(src):
        src = NEW_RE.sub(lambda _m: NULLENGINE, src, count=1)
        path.write_text(src, encoding="utf-8")
        print("[ok] nullengine create", path)
        return
    if "G1 cache cu data=" not in src:
        raise SystemExit("cu-only site not found")
    m = CU_TAIL_RE.search(src)
    if not m:
        raise SystemExit("cu tail not found")
    src = src[: m.end(1)] + ATTACH + src[m.end() :]
    path.write_text(src, encoding="utf-8")
    print("[ok] attach nullengine", path)


if __name__ == "__main__":
    main()
