#!/usr/bin/env python3
"""Rewrite GRUB GUI entries for the stub ISO.

PID1 stays named init.elf (daily kernel). Bytes are init_tramp.elf.
compositor.elf is a second module so exec_initrd can load it.
"""
from __future__ import annotations

import re
import sys

INIT_LINE = "module2 /boot/init_tramp.elf init.elf"
COMP_LINE = "module2 /boot/compositor.elf compositor.elf"
HELLO_LINE = "module2 /boot/hello.elf hello.elf"
P8_LINE = "module2 /boot/p8test.elf p8test.elf"


def rewrite_grub(text: str) -> str:
    parts = re.split(r"(?=menuentry\s)", text, flags=re.IGNORECASE)
    changed = 0
    out = []
    for part in parts:
        if "desktop.elf" in part:
            part, n = re.subn(
                r"module2\s+\S+\s+init\.elf",
                INIT_LINE + "\n    " + COMP_LINE + "\n    " + HELLO_LINE
                + "\n    " + P8_LINE,
                part,
            )
            changed += n
        out.append(part)
    text2 = "".join(out)
    if changed == 0:
        text2, n = re.subn(
            r"module2\s+/boot/initrd\.img\s+init\.elf(\r?\n)(\s*)module2\s+/boot/desktop\.elf\s+desktop\.elf",
            INIT_LINE + r"\1\2" + COMP_LINE + r"\1\2" + HELLO_LINE
            + r"\1\2" + P8_LINE
            + r"\1\2module2 /boot/desktop.elf desktop.elf",
            text,
        )
        changed = n
    if changed < 1:
        raise ValueError("no desktop.elf menuentry with an init.elf module")
    return text2


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write("usage: rewrite_grub_compositor_stub.py IN OUT\n")
        return 2
    data = open(argv[1], "r", encoding="utf-8", errors="replace").read()
    open(argv[2], "w", encoding="utf-8").write(rewrite_grub(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
