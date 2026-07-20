#!/usr/bin/env python3
"""Deduplicate top-level function/global definitions in syscall.c after failed replay.

Policy:
- Functions with bodies: keep LAST definition, delete earlier bodies.
- Object definitions (globals): keep FIRST, delete later duplicates.
- Leave forward declarations alone unless they are exact duplicate lines in a row.
"""
from __future__ import annotations

import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[2] / "kernel" / "sysmain" / "syscall.c"
text = PATH.read_text(encoding="utf-8", errors="replace")
lines = text.splitlines(keepends=True)

# Collect top-level function definitions: line index -> (name, end_exclusive)
# Heuristic: line matching `^(static )?(TYPE) name(` and next non-ws is `{` or `{` on same line,
# then brace-match. Skip lines that end with `;` (forward decls / prototypes).

TYPE = (
    r"(?:void|long|int|uint64_t|uint32_t|uint16_t|uint8_t|size_t|uintptr_t|unsigned|"
    r"char\s*\*|bfree_\w+_t(?:\s*\*)?)"
)
FN_START = re.compile(
    rf"^(static\s+)?{TYPE}\s+(\w+)\s*\((.*)\)\s*(\{{)?\s*$"
)
# Also match multi-line proto ending with ){ on later line — handled by looking ahead.

def is_forward_decl(line: str) -> bool:
    s = line.strip()
    return s.endswith(";") and "(" in s and ")" in s and "{" not in s


def find_functions(ls: list[str]) -> list[tuple[str, int, int]]:
    """Return list of (name, start_idx, end_idx_exclusive)."""
    out: list[tuple[str, int, int]] = []
    i = 0
    n = len(ls)
    while i < n:
        line = ls[i]
        if is_forward_decl(line):
            i += 1
            continue
        m = FN_START.match(line.rstrip("\n"))
        if not m:
            i += 1
            continue
        name = m.group(2)
        # Determine if body starts here or on following lines
        brace_line = i
        if m.group(4) != "{":
            # look ahead for `{` alone or `){`
            j = i + 1
            found = False
            while j < n and j < i + 6:
                t = ls[j].strip()
                if t == "{" or t.endswith("{") and not t.endswith(";"):
                    brace_line = j
                    found = True
                    break
                if t.endswith(";"):
                    break  # forward decl split across lines — skip
                j += 1
            if not found:
                i += 1
                continue
        # brace match from brace_line
        depth = 0
        k = brace_line
        started = False
        while k < n:
            for ch in ls[k]:
                if ch == "{":
                    depth += 1
                    started = True
                elif ch == "}":
                    depth -= 1
                    if started and depth == 0:
                        out.append((name, i, k + 1))
                        i = k + 1
                        break
            else:
                k += 1
                continue
            break
        else:
            i += 1
    return out


def find_globals(ls: list[str]) -> list[tuple[str, int, int]]:
    """Object definitions at column 0-ish (not inside functions). Approximate: lines at indent 0."""
    # Only scan lines that are not inside any function body we already found — do after
    # building a mask of function ranges.
    return []  # filled below


funcs = find_functions(lines)
by_name: dict[str, list[tuple[int, int]]] = {}
for name, s, e in funcs:
    by_name.setdefault(name, []).append((s, e))

# Mark ranges to delete: all but last for each multi-def function
delete = [False] * len(lines)
dup_fn_count = 0
for name, spans in by_name.items():
    if len(spans) <= 1:
        continue
    dup_fn_count += 1
    # keep last
    for s, e in spans[:-1]:
        for i in range(s, e):
            delete[i] = True
        # also delete a preceding blank line if present
        if s > 0 and lines[s - 1].strip() == "":
            delete[s - 1] = True

print(f"functions parsed: {len(funcs)}, duplicated names: {dup_fn_count}")

# Rebuild without deleted function bodies, then dedupe globals on the result
new_lines = [ln for i, ln in enumerate(lines) if not delete[i]]

# Global / static object defs (not functions): keep first, drop later
GLOB = re.compile(
    r"^(static\s+)?(?:const\s+)?(?:volatile\s+)?"
    r"(?:unsigned\s+)?"
    r"(?:uint64_t|uint32_t|uint16_t|uint8_t|int|long|char|size_t|uintptr_t|bfree_\w+_t)\s+"
    r"(\w+)\s*(?:\[|=|;)"
)
# Also bare: uint64_t g_foo;
GLOB2 = re.compile(
    r"^(?:static\s+)?(?:uint64_t|uint32_t|uint16_t|uint8_t|int|long|uintptr_t)\s+(g_\w+)\s*;"
)

seen_glob: set[str] = set()
delete2 = [False] * len(new_lines)
# Exclude function bodies again
funcs2 = find_functions(new_lines)
in_fn = [False] * len(new_lines)
for name, s, e in funcs2:
    for i in range(s, e):
        in_fn[i] = True

glob_dups = 0
for i, ln in enumerate(new_lines):
    if in_fn[i]:
        continue
    if is_forward_decl(ln):
        continue
    m = GLOB.match(ln.rstrip("\n"))
    if m:
        name = m.group(2)
    else:
        m = GLOB2.match(ln.rstrip("\n"))
        if not m:
            continue
        name = m.group(1)
    if not name.startswith("g_"):
        # only dedupe g_* for safety
        continue
    if name in seen_glob:
        delete2[i] = True
        glob_dups += 1
        # multi-line array init? rare for our dups; handle `[...] = {` blocks
        if "[" in ln and "=" in ln and "{" in ln and "}" not in ln:
            depth = ln.count("{") - ln.count("}")
            j = i + 1
            while j < len(new_lines) and depth > 0:
                delete2[j] = True
                depth += new_lines[j].count("{") - new_lines[j].count("}")
                j += 1
        continue
    seen_glob.add(name)

print(f"global duplicate lines removed: {glob_dups}")

# Also remove consecutive duplicate #define / identical forward-decl blocks
final = []
prev = None
skip_dup_defines = 0
for i, ln in enumerate(new_lines):
    if delete2[i]:
        continue
    # drop exact consecutive duplicate non-empty lines (common for forward decls)
    if ln.strip() and prev is not None and ln == prev and (
        ln.strip().startswith("#define")
        or ln.strip().startswith("static ")
        and ln.strip().endswith(";")
        or ln.strip().startswith("extern ")
    ):
        skip_dup_defines += 1
        continue
    final.append(ln)
    prev = ln

print(f"consecutive dup lines removed: {skip_dup_defines}")
print(f"lines {len(lines)} -> {len(final)}")

# Backup then write
bak = PATH.with_suffix(".c.pre_dedup")
if not bak.exists():
    bak.write_text(text, encoding="utf-8")
    print("backup", bak)
PATH.write_text("".join(final), encoding="utf-8")
print("wrote", PATH)
