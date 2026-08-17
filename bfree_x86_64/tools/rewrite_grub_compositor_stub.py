#!/usr/bin/env python3
"""Rewrite GRUB so the GUI (desktop.elf) entry loads compositor.elf as init.elf.

Daily kernel stays BFREE_BOOT_GUI_FIRST=0 and still looks for PID1 name init.elf.
The compositor stub bytes are registered under that name. Do not rebuild kernel.
"""
from __future__ import annotations

import re
import sys


def rewrite_grub(text: str) -> str:
    parts = re.split(r"(?=menuentry\s)", text)
    changed = 0
    out = []
    for part in parts:
        if "desktop.elf" in part:
            part, n = re.subn(
                r"module2\s+\S+\s+init\.elf",
                "module2 /boot/compositor.elf init.elf",
                part,
                count=1,
            )
            changed += n
        out.append(part)
    if changed != 1:
        raise ValueError(
            "expected exactly one init.elf module in a desktop.elf menuentry, got %d"
            % changed
        )
    return "".join(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write("usage: rewrite_grub_compositor_stub.py IN OUT\n")
        return 2
    data = open(argv[1], "r", encoding="utf-8", errors="replace").read()
    open(argv[2], "w", encoding="utf-8").write(rewrite_grub(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
