#!/usr/bin/env python3
"""Set priv->start=0 and url after populate. Dump only. No beginCreate."""
from __future__ import annotations

import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

HELPER_START = "static void guest_g1_instantiate_from_cached_unit"
HELPER_END = "static void guest_g1_post_loop_thin_qml"
ANCHOR = '            guest_serial_puts("[desktop_qt] G1 start=");\n'
SET = (
    "            priv->start = 0;\n"
    '            priv->url = QUrl(QStringLiteral("qrc:/GuestGate1Window.qml"));\n'
    + ANCHOR
)


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    start = src.find(HELPER_START)
    end = src.find(HELPER_END)
    if start < 0 or end <= start:
        raise SystemExit("helper not found")
    helper = src[start:end]
    if "priv->start = 0" in helper:
        print("[ok] already set start", path)
        return
    if "G1 beginCreate begin" in helper:
        raise SystemExit("beginCreate in helper; refuse")
    if ANCHOR not in helper:
        raise SystemExit("G1 start= dump missing; apply priv dump first")
    new_helper = helper.replace(ANCHOR, SET, 1)
    path.write_text(src[:start] + new_helper + src[end:], encoding="utf-8")
    print("[ok] set start", path)


if __name__ == "__main__":
    main()
