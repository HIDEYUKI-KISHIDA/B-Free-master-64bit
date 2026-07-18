#!/usr/bin/env python3
"""B-Free guest: avoid regcomp in simple grep (nofork / inproc pipe safety)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "B-Free: simple grep"
ANCHOR = "\targv += optind;\n"
INSERT = f"""\targv += optind;

\t/* {MARKER} — before load_pattern_list/regcomp */
\tif (!(option_mask32 & (OPT_i|OPT_v|OPT_x|OPT_w|OPT_r|OPT_R|OPT_q|OPT_l|OPT_c|OPT_n|OPT_h|OPT_H|OPT_o))) {{
\t\tconst char *pat = NULL;
\t\tFILE *in = NULL;
\t\tconst char *in_name = NULL;
\t\tif (pattern_head && !pattern_head->link) {{
\t\t\tpat = ((grep_list_data_t *)pattern_head->data)->pattern;
\t\t\tif (argv[0] && !argv[1] && !LONE_DASH(argv[0])) {{
\t\t\t\tin_name = argv[0];
\t\t\t\tin = fopen_for_read(argv[0]);
\t\t\t}} else if (!argv[0]) {{
\t\t\t\tin = stdin;
\t\t\t}}
\t\t}} else if (pattern_head == NULL && argv[0] && argv[1] && !argv[2]) {{
\t\t\tpat = argv[0];
\t\t\tin_name = argv[1];
\t\t\tin = fopen_for_read(argv[1]);
\t\t}} else if (pattern_head == NULL && argv[0] && !argv[1]) {{
\t\t\tpat = argv[0];
\t\t\tin = stdin;
\t\t}}
\t\tif (pat && (in || in == stdin)) {{
\t\t\tif (in != stdin && !in) {{
\t\t\t\tif (!SUPPRESS_ERR_MSGS)
\t\t\t\t\tbb_simple_perror_msg(in_name);
\t\t\t\treturn 2;
\t\t\t}}
\t\t\tfor (;;) {{
\t\t\t\tchar *line = xmalloc_fgetline(in);
\t\t\t\tif (!line)
\t\t\t\t\tbreak;
\t\t\t\tif (strstr(line, pat)) {{
\t\t\t\t\tputs(line);
\t\t\t\t\tmatched = 1;
\t\t\t\t}}
\t\t\t\tfree(line);
\t\t\t}}
\t\t\tif (in != stdin)
\t\t\t\tfclose(in);
\t\t\treturn !matched;
\t\t}}
\t}}
"""


def strip_old_patches(text: str) -> str:
    text = re.sub(
        r"\t/\* B-Free: simple stdin grep \*/\n\t\{.*?\n\t\}\n\n",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )
    text = re.sub(
        rf"\t/\* {re.escape(MARKER)}.*?\n\t\}}\n(?:bfree_grep_full:\n\n)?",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )
    text = re.sub(
        rf"\t/\* {re.escape(MARKER)} — before load_pattern_list/regcomp \*/\n\tif.*?\n\t\}}\n",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )
    text = text.replace("\targv += optind;\n\n\targv += optind;\n", "\targv += optind;\n")
    return text


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_grep_simple.py /path/to/grep.c", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = strip_old_patches(path.read_text(encoding="utf-8", errors="replace"))
    if ANCHOR not in text:
        print(f"[patch] ERROR: anchor not found in {path}", file=sys.stderr)
        return 1
    path.write_text(text.replace(ANCHOR, INSERT, 1), encoding="utf-8")
    print(f"[patch] applied simple grep to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
