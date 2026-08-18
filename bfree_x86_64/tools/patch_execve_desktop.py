#!/usr/bin/env python3
"""Insert desktop.elf into sys_linux_execve's initrd image map.

Does not overwrite the rest of syscall.c. Idempotent.
Usage: python3 patch_execve_desktop.py kernel/sysmain/syscall.c
"""
from __future__ import annotations

import sys
from pathlib import Path

MAP_OLD = """        } else if (bfree_guest_basename_eq(path, "abi_hole_finder.elf")) {
            img = "abi_hole_finder.elf";
        }
        exec_img = img;
"""

MAP_NEW = """        } else if (bfree_guest_basename_eq(path, "abi_hole_finder.elf")) {
            img = "abi_hole_finder.elf";
        } else if (bfree_guest_basename_eq(path, "desktop.elf")) {
            img = "desktop.elf";
        }
        exec_img = img;
"""

STACK_OLD = """        if (bfree_user_stack_ensure_pages(stack_top,
                BFREE_USER_STACK_PAGES_BUSYBOX + BFREE_VFORK_EXEC_STACK_SLOT_PAGES) != 0) {
"""

STACK_NEW = """        if (bfree_user_stack_ensure_pages(stack_top,
                (bfree_guest_basename_eq(exec_img, "desktop.elf")
                     ? BFREE_USER_STACK_PAGES_DESKTOP
                     : BFREE_USER_STACK_PAGES_BUSYBOX) +
                    BFREE_VFORK_EXEC_STACK_SLOT_PAGES) != 0) {
"""

LOG_OLD = '    uart_puts("[ELF] exec transfer busybox gthr cleared entry=");\n'
LOG_NEW = (
    '    uart_puts("[ELF] exec transfer ");\n'
    "    uart_puts(exec_img);\n"
    '    uart_puts(" gthr cleared entry=");\n'
)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write("usage: patch_execve_desktop.py syscall.c\n")
        return 2
    path = Path(argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")
    n = 0
    if 'img = "desktop.elf"' not in text:
        if MAP_OLD not in text:
            sys.stderr.write("marker missing: abi_hole_finder.elf map\n")
            return 2
        text = text.replace(MAP_OLD, MAP_NEW, 1)
        n += 1
    if STACK_OLD in text:
        text = text.replace(STACK_OLD, STACK_NEW, 1)
        n += 1
    if LOG_OLD in text:
        text = text.replace(LOG_OLD, LOG_NEW, 1)
        n += 1
    path.write_text(text, encoding="utf-8")
    print("EXECVE_DESKTOP=patched" if n else "EXECVE_DESKTOP=already")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
