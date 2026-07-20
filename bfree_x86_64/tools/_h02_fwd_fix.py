#!/usr/bin/env python3
from pathlib import Path
PATH = Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/kernel/sysmain/syscall.c")
text = PATH.read_text(encoding="utf-8", errors="replace")
needle = "static void bfree_coop_as_switch_to(int side);\n"
extra = (
    needle
    + "static void bfree_coop_save_child_user(void);\n"
    + "static void bfree_coop_save_parent_user(void);\n"
    + "static void bfree_coop_publish_parent_resume(void);\n"
    + "static void bfree_coop_publish_child_resume(void);\n"
)
if "static void bfree_coop_save_child_user(void);" not in text:
    if needle not in text:
        raise SystemExit("FAIL: as_switch fwd")
    text = text.replace(needle, extra, 1)
    PATH.write_text(text, encoding="utf-8", newline="\n")
    print("OK added save/publish forwards")
else:
    print("skip already present")
