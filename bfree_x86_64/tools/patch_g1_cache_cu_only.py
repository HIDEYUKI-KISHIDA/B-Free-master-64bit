#!/usr/bin/env python3
"""After proven qmlData words, construct CompilationUnit only.

Do not call ExecutableCompilationUnit::create(). No loadUrl / beginCreate.
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

SKIP_RE = re.compile(
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 cache instantiate skip create\\n\"\);\r?\n"
    r".*?"
    r"[ \t]*g_g1_done = 1;\r?\n",
    re.S,
)

CU_BODY = r'''    guest_serial_puts("[desktop_qt] G1 cache instantiate skip create\n");
    guest_serial_puts("[desktop_qt] G1 qmlData=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cached->qmlData);
    guest_serial_puts("\n");
    const quint32 *w = reinterpret_cast<const quint32 *>(cached->qmlData);
    guest_serial_puts("[desktop_qt] G1 qmlData w0=");
    guest_serial_hex_u64((uint64_t)w[0]);
    guest_serial_puts(" w1=");
    guest_serial_hex_u64((uint64_t)w[1]);
    guest_serial_puts(" w2=");
    guest_serial_hex_u64((uint64_t)w[2]);
    guest_serial_puts(" w3=");
    guest_serial_hex_u64((uint64_t)w[3]);
    guest_serial_puts("\n");
    guest_serial_puts("[desktop_qt] G1 cache cu begin\n");
    QQmlRefPointer<QV4::CompiledData::CompilationUnit> cu(
        new QV4::CompiledData::CompilationUnit);
    cu->data = cached->qmlData;
    cu->aotCompiledFunctions = cached->aotCompiledFunctions;
    guest_serial_puts("[desktop_qt] G1 cache cu ok\n");
    guest_serial_puts("[desktop_qt] G1 cache cu data=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cu->data);
    guest_serial_puts("\n");
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
    if "G1 cache cu begin" in src and "ExecutableCompilationUnit::create" not in src:
        print("[ok] already cu only", path)
        return
    if "ExecutableCompilationUnit::create" in src:
        raise SystemExit("create() still present; refuse")
    if "G1 cache instantiate skip create" not in src:
        raise SystemExit("skip create site not found")
    m = SKIP_RE.search(src)
    if not m:
        raise SystemExit("skip-create block not found")
    src = src[: m.start()] + CU_BODY + src[m.end() :]
    path.write_text(src, encoding="utf-8")
    print("[ok] cu only", path)


if __name__ == "__main__":
    main()
