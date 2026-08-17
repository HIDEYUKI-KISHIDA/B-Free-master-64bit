#!/usr/bin/env python3
"""After proven Ready, call beginCreate only. No completeCreate / loadUrl / Wayland."""
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

OLD = re.compile(
    r"[ \t]*if \(g_g1_comp->isReady\(\)\)\r?\n"
    r"[ \t]*guest_serial_puts\(\"\[desktop_qt\] G1 thin QML Ready\\n\"\);\r?\n"
)

NEW = r'''    if (g_g1_comp->isReady()) {
        guest_serial_puts("[desktop_qt] G1 thin QML Ready\n");
        guest_serial_puts("[desktop_qt] G1 beginCreate begin\n");
        QObject *obj = g_g1_comp->beginCreate(g_engine->rootContext());
        guest_serial_puts("[desktop_qt] G1 beginCreate end\n");
        if (!obj)
            guest_serial_puts("[desktop_qt] G1 beginCreate null\n");
        else {
            guest_serial_puts("[desktop_qt] G1 beginCreate obj=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)obj);
            guest_serial_puts("\n");
        }
    }
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
    if "G1 beginCreate begin" in src:
        print("[ok] already beginCreate", path)
        return
    if "G1 thin QML Ready" not in src:
        raise SystemExit("Ready site not found")
    m = OLD.search(src)
    if not m:
        raise SystemExit("isReady Ready puts not found")
    src = src[: m.start()] + NEW + src[m.end() :]
    path.write_text(src, encoding="utf-8")
    print("[ok] beginCreate", path)


if __name__ == "__main__":
    main()
