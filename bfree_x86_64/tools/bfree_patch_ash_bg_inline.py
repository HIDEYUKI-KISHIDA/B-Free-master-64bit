#!/usr/bin/env python3
"""Make ash 'cmd &' safe on B-Free cooperative fork (shared address space)."""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "B-Free: bg inline"

OLD = """\tstruct job *jp;
\tint backgnd = (n->type == NBACKGND); /* FORK_BG(1) if yes, else FORK_FG(0) */
\tint status;

\terrlinno = lineno = n->nredir.linno;

\texpredir(n->nredir.redirect);
\tif (!backgnd && (flags & EV_EXIT) && !may_have_traps)
\t\tgoto nofork;"""

NEW = f"""\tstruct job *jp;
\tint backgnd = (n->type == NBACKGND); /* FORK_BG(1) if yes, else FORK_FG(0) */
\tint status;

\terrlinno = lineno = n->nredir.linno;

\texpredir(n->nredir.redirect);
\t/* {MARKER}: cooperative fork shares address space; child would corrupt
\t * ash/stdio. Run background commands inline (no real async). */
\tif (backgnd) {{
\t\tstatus = evaltree(n->nredir.n, flags);
\t\treturn status;
\t}}
\tif (!backgnd && (flags & EV_EXIT) && !may_have_traps)
\t\tgoto nofork;"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_ash_bg_inline.py /path/to/ash.c", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in text:
        print(f"[patch] already applied: {path}")
        return 0
    if OLD not in text:
        print(f"[patch] ERROR: evalsubshell prologue not found in {path}", file=sys.stderr)
        return 1
    path.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    print(f"[patch] applied bg-inline to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
