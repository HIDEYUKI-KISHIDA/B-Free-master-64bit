#!/usr/bin/env python3
"""Second-pass structural repairs."""
from __future__ import annotations

import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[2] / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
orig = text

# --- Move rich vfile typedef before first use ---
rich_re = re.compile(
    r"typedef struct \{\n"
    r"    int used;\n"
    r"    int is_symlink; /\* data\[\] holds the link target instead of file contents \*/\n"
    r"    int is_dir;.*?\} bfree_guest_vfile_t;\n",
    re.S,
)
ms = list(rich_re.finditer(text))
print("rich vfile matches", len(ms))
if ms:
    rich = ms[0].group(0)
    # remove all rich typedefs
    text2 = rich_re.sub("", text)
    # place at simple-removed comment or before g_guest_vfiles
    marker = "/* simple bfree_guest_vfile_t removed; richer typedef kept below */\n"
    if marker in text2:
        text2 = text2.replace(marker, rich + "\n", 1)
        print("OK placed rich vfile at removed-simple marker")
    else:
        needle = "static bfree_guest_vfile_t g_guest_vfiles["
        idx = text2.find(needle)
        if idx >= 0:
            text2 = text2[:idx] + rich + "\n" + text2[idx:]
            print("OK placed rich vfile before g_guest_vfiles")
        else:
            print("MISS place for rich vfile")
    text = text2

# --- Fix linux_user_kva signature ---
old_kva = """static void *bfree_linux_user_kva(uint64_t uaddr)
{
    (void)uaddr;
    return (void *)(uintptr_t)uaddr; /* identity user map in B-Free guest */
}
"""
new_kva = """static void *bfree_linux_user_kva(page_table_t *pt, uint64_t uaddr)
{
    (void)pt;
    return (void *)(uintptr_t)uaddr; /* identity user map in B-Free guest */
}
"""
if old_kva in text:
    text = text.replace(old_kva, new_kva, 1)
    print("OK kva signature")
else:
    # try regex
    text3, n = re.subn(
        r"static void \*bfree_linux_user_kva\(uint64_t uaddr\)\n\{[^}]*\}",
        new_kva.strip(),
        text,
        count=1,
    )
    if n:
        text = text3
        print("OK kva signature (re)")
    else:
        print("MISS kva")

# --- Fix exit_reenter: call has no args ---
text = text.replace(
    "static long sys_linux_syscall_dispatch_exit_reenter(long status);",
    "static long sys_linux_syscall_dispatch_exit_reenter(void);",
)
old_er = """static long sys_linux_syscall_dispatch_exit_reenter(long status)
{
    return bfree_guest_exit_from_fork(status);
}
"""
new_er = """static long sys_linux_syscall_dispatch_exit_reenter(void)
{
    return bfree_guest_exit_from_fork(0);
}
"""
if old_er in text:
    text = text.replace(old_er, new_er, 1)
    print("OK exit_reenter void")
else:
    text4, n = re.subn(
        r"static long sys_linux_syscall_dispatch_exit_reenter\(long status\)\n\{[^}]*\}",
        new_er.strip(),
        text,
        count=1,
    )
    if n:
        text = text4
        print("OK exit_reenter void (re)")
    else:
        print("MISS exit_reenter body")

# Forward-declare exit_from_fork before exit_reenter stub
if "static long bfree_guest_exit_from_fork(long status);" not in text:
    text = text.replace(
        "static long sys_linux_syscall_dispatch_exit_reenter(void);",
        "static long bfree_guest_exit_from_fork(long status);\nstatic long sys_linux_syscall_dispatch_exit_reenter(void);",
        1,
    )
    print("OK forward decl exit_from_fork")

# --- Fix orphaned /etc/profile strings ---
orphan_prof = '''    "# B-Free guest profile\\n"
    "export PATH=/bin:/usr/bin:.\\n"
    "export PS1='root@bfree:# '\\n";

static const char g_guest_etc_motd[] = "";
'''
fixed_prof = '''static const char g_guest_etc_profile[] =
    "# B-Free guest profile\\n"
    "export PATH=/bin:/usr/bin:.\\n"
    "export PS1='root@bfree:# '\\n";
static size_t g_guest_etc_profile_off;

static const char g_guest_etc_motd[] = "";
'''
if orphan_prof in text:
    text = text.replace(orphan_prof, fixed_prof, 1)
    print("OK etc_profile")
else:
    text5, n = re.subn(
        r'    "# B-Free guest profile\\n"\n'
        r'    "export PATH=/bin:/usr/bin:\.\\n"\n'
        r"    \"export PS1='root@bfree:# '\\\\n\";\n\n"
        r"static const char g_guest_etc_motd\[\] = \"\";\n",
        fixed_prof,
        text,
        count=1,
    )
    if n:
        text = text5
        print("OK etc_profile (re)")
    else:
        print("MISS etc_profile")

# --- Dedup proc_maps_desktop if duplicated ---
# Keep first occurrence of each
for name in [
    "static const char g_guest_proc_maps_desktop[]",
    "static const char g_guest_proc_maps_busybox[]",
    "static const char *g_guest_proc_maps =",
]:
    idxs = [m.start() for m in re.finditer(re.escape(name), text)]
    if len(idxs) > 1:
        print("dup", name, len(idxs))

# Remove SECOND g_guest_proc_maps_desktop block through its terminating `;`
def remove_nth_array(text: str, start_pat: str, which: int) -> str:
    matches = list(re.finditer(start_pat, text))
    if len(matches) <= which:
        return text
    start = matches[which].start()
    # find end at first `;\n` after start that closes the array (heuristic: line ending with `";`)
    i = start
    while i < len(text):
        if text[i] == ";" and i + 1 < len(text) and text[i + 1] == "\n":
            # ensure we're past opening
            if i > start + 20:
                return text[:start] + text[i + 2 :]
        i += 1
    return text

if text.count("static const char g_guest_proc_maps_desktop[]") > 1:
    text = remove_nth_array(text, r"static const char g_guest_proc_maps_desktop\[\]", 1)
    print("OK removed 2nd proc_maps_desktop")
if text.count("static const char g_guest_proc_maps_busybox[]") > 1:
    text = remove_nth_array(text, r"static const char g_guest_proc_maps_busybox\[\]", 1)
    print("OK removed 2nd proc_maps_busybox")
# pointer assigns
while text.count("static const char *g_guest_proc_maps =") > 1:
    # remove last
    idx = text.rfind("static const char *g_guest_proc_maps =")
    end = text.find("\n", idx)
    text = text[:idx] + text[end + 1 :]
    print("OK removed dup g_guest_proc_maps ptr")

# --- Ensure fd snap arrays exist before first use, after FD_TABLE_SIZE ---
if "static int g_fd_snap_parent[" in text:
    # move definition right after BFREE_GUEST_FD_TABLE_SIZE define if used before def
    pass

# Extract existing snap defs
snap_re = re.compile(
    r"static int g_fd_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"(?:static uint8_t g_fd_cloexec_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static uint8_t g_fd_cloexec_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n)?"
    r"static int g_fd_dup_save_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_dup_save_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
)
# Also our early incomplete insert with ifndef
early_snap = re.compile(
    r"#ifndef BFREE_GUEST_FD_TABLE_SIZE\n#define BFREE_GUEST_FD_TABLE_SIZE 64\n#endif\n"
    r"static int g_fd_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_dup_save_snap_parent\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
    r"static int g_fd_dup_save_snap_child\[BFREE_GUEST_FD_TABLE_SIZE\];\n"
)

# Ensure BFREE_GUEST_FD_TABLE_SIZE is defined early
if "#define BFREE_GUEST_FD_TABLE_SIZE" not in text.split("g_fd_snap_parent")[0]:
    # FD table size defined after first use — hoist a define near top after includes
    if "#define BFREE_GUEST_FD_TABLE_SIZE" in text:
        # already somewhere — add early define if missing before first snap use
        pass
    text = text.replace(
        "extern page_table_t kernel_page_table;\n",
        "extern page_table_t kernel_page_table;\n\n#ifndef BFREE_GUEST_FD_TABLE_SIZE\n"
        "#define BFREE_GUEST_FD_TABLE_SIZE 64\n#endif\n"
        "static int g_fd_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];\n"
        "static int g_fd_snap_child[BFREE_GUEST_FD_TABLE_SIZE];\n"
        "static int g_fd_dup_save_snap_parent[BFREE_GUEST_FD_TABLE_SIZE];\n"
        "static int g_fd_dup_save_snap_child[BFREE_GUEST_FD_TABLE_SIZE];\n",
        1,
    )
    print("OK early FD_TABLE + snap arrays at top")
    # remove later duplicate snap array defs
    text6, n = snap_re.subn("", text)
    if n:
        text = text6
        print("OK removed", n, "later snap blocks")
    text7, n = early_snap.subn("", text)
    if n:
        text = text7
        print("OK removed", n, "early_snap blocks from mid-file")

# --- Dedup tmp_child_basename: keep last body via re-run style ---
# Find all definitions
fn = "bfree_guest_tmp_child_basename"
# crude: if redefinition error, delete first definition body
pat = re.compile(
    rf"static int {fn}\(const char \*entry_name,[\s\S]*?\n\}}\n"
)
ms = list(pat.finditer(text))
print(f"{fn} defs", len(ms))
if len(ms) > 1:
    # remove all but last
    for m in reversed(ms[:-1]):
        text = text[: m.start()] + text[m.end() :]
    print("OK kept last tmp_child_basename")

# Fix forward decl of bfree_linux_stat_fill — remove wrong early prototype if real has different sig
# Look at actual definition
m = re.search(
    r"static long bfree_linux_stat_fill\(([^)]*)\)\s*\{",
    text,
)
if m:
    args = m.group(1)
    print("stat_fill args:", args.replace("\n", " "))
    # replace early prototype
    text = re.sub(
        r"static long bfree_linux_stat_fill\([^;]*\);",
        f"static long bfree_linux_stat_fill({args});",
        text,
        count=1,
    )
    print("OK synced stat_fill prototype")

# Remove duplicate typedef bfree_linux_stat_t keep first? or last?
stat_td = list(
    re.finditer(r"typedef struct \{[\s\S]*?\} bfree_linux_stat_t;\n", text)
)
print("linux_stat_t typedefs", len(stat_td))
if len(stat_td) > 1:
    for m in reversed(stat_td[1:]):
        text = text[: m.start()] + text[m.end() :]
    print("OK removed dup linux_stat_t")

if text != orig:
    PATH.write_text(text, encoding="utf-8")
    print("wrote", PATH, "delta", len(text) - len(orig))
else:
    print("no change")
