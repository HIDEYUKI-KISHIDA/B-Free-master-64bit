#!/usr/bin/env python3
"""Third-pass: fd_snap hoist, restore 3-arg stat_fill, remove remaining dups."""
from __future__ import annotations

import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[2] / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

# --- Hoist fd snap arrays to just after includes / kernel_page_table ---
snap_block = """
#ifndef BFREE_GUEST_FD_TABLE_SIZE
#define BFREE_GUEST_FD_TABLE_SIZE 64
#endif
static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];
static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];
"""

# Remove all existing snap array definitions (keep one early)
text = re.sub(
    r"(?:#ifndef BFREE_GUEST_FD_TABLE_SIZE\n#define BFREE_GUEST_FD_TABLE_SIZE 64\n#endif\n)?"
    r"static int g_fd_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"(?:static uint8_t g_fd_cloexec_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static uint8_t g_fd_cloexec_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n)?"
    r"static int g_fd_dup_save_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_dup_save_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n",
    "",
    text,
)
if "extern page_table_t kernel_page_table;\n" in text:
    text = text.replace(
        "extern page_table_t kernel_page_table;\n",
        "extern page_table_t kernel_page_table;\n" + snap_block,
        1,
    )
    print("OK hoisted fd snap")
else:
    print("MISS kernel_page_table for snap")

# --- Replace 2-arg stat_fill with 3-arg ---
best = (Path(__file__).resolve().parent / "_best_stat_fill.c").read_text(encoding="utf-8")
# remove all current definitions
text2, n = re.subn(
    r"static long bfree_linux_stat_fill\([^)]*\)\s*\{(?:[^{}]|\{[^{}]*\})*\}",
    "",
    text,
    count=0,
)
# The nested brace regex may be too weak; do brace match manually
def remove_all_stat_fill(s: str) -> str:
    while True:
        m = re.search(r"static long bfree_linux_stat_fill\(", s)
        if not m:
            return s
        start = m.start()
        brace = s.find("{", start)
        depth = 0
        j = brace
        while j < len(s):
            if s[j] == "{":
                depth += 1
            elif s[j] == "}":
                depth -= 1
                if depth == 0:
                    s = s[:start] + s[j + 1 :]
                    break
            j += 1
        else:
            return s

text = remove_all_stat_fill(text)
# also remove prototypes
text = re.sub(r"static long bfree_linux_stat_fill\([^;]*\);\n?", "", text)

# Insert best near first call or after linux_stat_t typedef
td = text.find("} bfree_linux_stat_t;")
if td < 0:
    # insert before first call
    call = text.find("bfree_linux_stat_fill(")
    insert_at = text.rfind("\n", 0, call) if call > 0 else 0
else:
    insert_at = td + len("} bfree_linux_stat_t;")
text = text[:insert_at] + "\n\n" + best + "\n" + text[insert_at:]
print("OK restored 3-arg stat_fill")

# --- Dedup remaining functions: keep last ---
for fname in [
    "bfree_guest_vfile_from_fd",
    "bfree_guest_vfile_find_by_name",
]:
    spans = []
    for m in re.finditer(rf"static\s+\w[\w\s\*]*\s+{fname}\s*\(", text):
        # skip prototypes ending with ;
        line_end = text.find("\n", m.start())
        chunk = text[m.start() : line_end]
        if chunk.rstrip().endswith(";"):
            continue
        brace = text.find("{", m.start())
        if brace < 0 or brace > m.start() + 200:
            continue
        depth = 0
        j = brace
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    spans.append((m.start(), j + 1))
                    break
            j += 1
    print(fname, "defs", len(spans))
    if len(spans) > 1:
        for s, e in reversed(spans[:-1]):
            text = text[:s] + text[e:]
        print("OK kept last", fname)

# --- Dedup etc_profile ---
# keep first full array
idxs = [m.start() for m in re.finditer(r"static const char g_guest_etc_profile\[\]", text)]
print("etc_profile", idxs)
if len(idxs) > 1:
    # remove from second start through its terminating ;
    start = idxs[1]
    i = start
    while i < len(text):
        if text[i] == ";" and text[i + 1 : i + 2] == "\n":
            # also remove following off var if present
            end = i + 2
            if text[end : end + 40].startswith("static size_t g_guest_etc_profile_off"):
                end = text.find("\n", end) + 1
            text = text[:start] + text[end:]
            print("OK removed 2nd etc_profile")
            break
        i += 1

# --- Dedup k_usr_names / k_var_names ---
for name in ["k_usr_names", "k_var_names"]:
    # static const char * const k_usr_names[] = { ... };
    pat = re.compile(
        rf"static const char \* ?(?:const )?{name}\[\] = \{{.*?\}};\n",
        re.S,
    )
    ms = list(pat.finditer(text))
    print(name, len(ms))
    if len(ms) > 1:
        for m in reversed(ms[1:]):
            text = text[: m.start()] + text[m.end() :]
        print("OK dedup", name)

# --- BFREE_GUEST_DEV_TTY_FD ---
if "BFREE_GUEST_DEV_TTY_FD" not in text.split("BFREE_GUEST_DEV_TTY_FD")[0] if "BFREE_GUEST_DEV_TTY_FD" in text else True:
    # add near other DEV fds
    if "#define BFREE_GUEST_DEV_NULL_FD" in text and "#define BFREE_GUEST_DEV_TTY_FD" not in text:
        text = text.replace(
            "#define BFREE_GUEST_DEV_NULL_FD",
            "#define BFREE_GUEST_DEV_TTY_FD     0x3707\n#define BFREE_GUEST_DEV_NULL_FD",
            1,
        )
        print("OK DEV_TTY_FD")
    elif "#define BFREE_GUEST_DEV_TTY_FD" in text:
        print("TTY already defined")
    else:
        # force near urandom if present
        if "BFREE_GUEST_DEV_URANDOM_FD" in text and "BFREE_GUEST_DEV_TTY_FD" not in text:
            text = text.replace(
                "#define BFREE_GUEST_DEV_URANDOM_FD",
                "#define BFREE_GUEST_DEV_URANDOM_FD",
                1,
            )
            # insert after urandom line
            m = re.search(r"#define BFREE_GUEST_DEV_URANDOM_FD[^\n]*\n", text)
            if m:
                text = text[: m.end()] + "#define BFREE_GUEST_DEV_TTY_FD       0x3707\n" + text[m.end() :]
                print("OK DEV_TTY_FD after urandom")
            else:
                print("MISS TTY insert")
        else:
            print("MISS DEV_NULL for TTY")

# HOME_DIR_FD
if "BFREE_GUEST_HOME_DIR_FD" in text and "#define BFREE_GUEST_HOME_DIR_FD" not in text:
    # only uses, no define — add
    m = re.search(r"#define BFREE_GUEST_TMP_DIR_FD[^\n]*\n", text)
    if m:
        text = text[: m.end()] + "#define BFREE_GUEST_HOME_DIR_FD      0x3720\n" + text[m.end() :]
        print("OK HOME_DIR_FD")

PATH.write_text(text, encoding="utf-8")
print("wrote delta", len(text) - len(orig))
