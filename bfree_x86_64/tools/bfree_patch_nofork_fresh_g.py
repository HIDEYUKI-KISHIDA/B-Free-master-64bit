#!/usr/bin/env python3
"""B-Free: zero bb_common_bufsiz1 before each nofork applet main().

Many applets keep their globals struct G inside bb_common_bufsiz1 and rely
on it being fresh BSS (fork+exec semantics). With the nofork-all patch the
buffer persists across applet runs, so e.g. a prior `sed -n p` leaves
be_quiet=1 and stale heap pointers behind; the next `sed s///` prints
nothing on Linux and reallocs a dangling pointer on the B-Free guest
(mallocng get_meta assert -> GPF). Zeroing the buffer restores per-run
BSS semantics.
"""
import sys
from pathlib import Path

MARKER = "B-Free: fresh common_bufsiz1"
ANCHOR = "\t\tclearerr(stdin); /* B-Free */\n"
INSERT = (
    ANCHOR
    + "\t\t{ /* "
    + MARKER
    + " */\n"
    + "\t\t\textern char bb_common_bufsiz1[];\n"
    + "\t\t\tmemset(bb_common_bufsiz1, 0, 1024);\n"
    + "\t\t}\n"
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_nofork_fresh_g.py /path/to/vfork_daemon_rexec.c",
              file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in text:
        print(f"[patch] fresh common_bufsiz1 already applied to {path}")
        return 0
    if ANCHOR not in text:
        print(f"[patch] ERROR: clearerr anchor not found in {path}", file=sys.stderr)
        return 1
    path.write_text(text.replace(ANCHOR, INSERT, 1), encoding="utf-8")
    print(f"[patch] applied fresh common_bufsiz1 to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
