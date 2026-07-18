#!/usr/bin/env python3
"""B-Free guest: literal s/pat/rep/ sed without musl regcomp (mallocng GPF)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "B-Free: simple sed"
FUNCS = f"""/* {MARKER} */
static int bfree_sed_is_literal_token(const char *s)
{{
\twhile (*s) {{
\t\tunsigned char c = (unsigned char)*s++;
\t\tif ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
\t\t    (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')
\t\t\tcontinue;
\t\treturn 0;
\t}}
\treturn 1;
}}

static int bfree_sed_simple_main(int argc, char **argv)
{{
\tconst char *expr, *p1, *p2, *p3;
\tchar pat[128], rep[128];
\tsize_t plen, rlen;
\tFILE *in;
\tint matched;

\tif (argc < 2 || argc > 3 || argv[1][0] != 's' || argv[1][1] != '/')
\t\treturn -1;
\texpr = argv[1];
\tp1 = expr + 2;
\tp2 = strchr(p1, '/');
\tif (!p2)
\t\treturn -1;
\tp3 = strchr(p2 + 1, '/');
\tif (!p3 || p3[1] != '\\0')
\t\treturn -1;
\tplen = (size_t)(p2 - p1);
\trlen = (size_t)(p3 - (p2 + 1));
\tif (plen == 0 || plen >= sizeof(pat) || rlen >= sizeof(rep))
\t\treturn -1;
\tmemcpy(pat, p1, plen);
\tpat[plen] = '\\0';
\tmemcpy(rep, p2 + 1, rlen);
\trep[rlen] = '\\0';
\tif (!bfree_sed_is_literal_token(pat) || !bfree_sed_is_literal_token(rep))
\t\treturn -1;
\tin = stdin;
\tif (argc == 3) {{
\t\tin = fopen_for_read(argv[2]);
\t\tif (!in) {{
\t\t\tbb_simple_perror_msg(argv[2]);
\t\t\treturn 1;
\t\t}}
\t}}
\tmatched = 0;
\tfor (;;) {{
\t\tchar *line = xmalloc_fgetline(in);
\t\tchar *hit, *out;
\t\tif (!line)
\t\t\tbreak;
\t\thit = strstr(line, pat);
\t\tif (!hit) {{
\t\t\tputs(line);
\t\t}} else {{
\t\t\tout = xmalloc(strlen(line) + rlen + 1);
\t\t\tmemcpy(out, line, (size_t)(hit - line));
\t\t\tout[hit - line] = '\\0';
\t\t\tstrcat(out, rep);
\t\t\tstrcat(out, hit + plen);
\t\t\tputs(out);
\t\t\tfree(out);
\t\t\tmatched = 1;
\t\t}}
\t\tfree(line);
\t}}
\tif (in != stdin)
\t\tfclose(in);
\treturn 0;
}}

"""
CALL = f"""
\t/* {MARKER} */
\t{{
\t\tint sr = bfree_sed_simple_main(argc, argv);
\t\tif (sr >= 0)
\t\t\treturn sr;
\t}}
"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_sed_simple.py /path/to/sed.c", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in text:
        print(f"[patch] simple sed already applied to {path}")
        return 0
    anchor_main = "int sed_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;\n"
    if anchor_main not in text:
        print(f"[patch] ERROR: sed_main anchor not found in {path}", file=sys.stderr)
        return 1
    text = text.replace(anchor_main, FUNCS + anchor_main, 1)
    anchor_init = "\tINIT_G();\n"
    if anchor_init not in text:
        print(f"[patch] ERROR: INIT_G anchor not found in {path}", file=sys.stderr)
        return 1
    text = text.replace(anchor_init, anchor_init + CALL, 1)
    path.write_text(text, encoding="utf-8")
    print(f"[patch] applied simple sed to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
