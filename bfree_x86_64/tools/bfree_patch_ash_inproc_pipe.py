#!/usr/bin/env python3
"""Replace ash evalpipe fork loop with in-process sequential pipes for B-Free guest."""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "B-Free: inproc pipe"

OLD = """\tfor (lp = n->npipe.cmdlist; lp; lp = lp->next) {
\t\tprehash(lp->n);
\t\tpip[1] = -1;
\t\tif (lp->next) {
\t\t\tif (pipe(pip) < 0) {
\t\t\t\tclose(prevfd);
\t\t\t\tash_msg_and_raise_perror("can't create pipe");
\t\t\t}
\t\t}
\t\tif (forkshell(jp, lp->n, n->npipe.pipe_backgnd) == 0) {
\t\t\t/* child */
\t\t\tINT_ON;
\t\t\tif (pip[1] >= 0) {
\t\t\t\tclose(pip[0]);
\t\t\t}
\t\t\tif (prevfd > 0) {
\t\t\t\tdup2(prevfd, 0);
\t\t\t\tclose(prevfd);
\t\t\t}
\t\t\tif (pip[1] > 1) {
\t\t\t\tdup2(pip[1], 1);
\t\t\t\tclose(pip[1]);
\t\t\t}
\t\t\tevaltreenr(lp->n, flags);
\t\t\t/* never returns */
\t\t}
\t\t/* parent */
\t\tif (prevfd >= 0)
\t\t\tclose(prevfd);
\t\tprevfd = pip[0];
\t\t/* Don't want to trigger debugging */
\t\tif (pip[1] != -1)
\t\t\tclose(pip[1]);
\t}
\tif (n->npipe.pipe_backgnd == 0) {
\t\tstatus = waitforjob(jp);
\t\tTRACE(("evalpipe:  job done exit status %d\\n", status));
\t}"""

NEW = f"""\t/* {MARKER} */
\tflags &= ~EV_EXIT;
\tfor (lp = n->npipe.cmdlist; lp; lp = lp->next) {{
\t\tint save0 = -1;
\t\tint save1 = -1;

\t\tprehash(lp->n);
\t\tpip[1] = -1;
\t\tif (lp->next) {{
\t\t\tif (pipe(pip) < 0) {{
\t\t\t\tif (prevfd >= 0)
\t\t\t\t\tclose(prevfd);
\t\t\t\tash_msg_and_raise_perror("can't create pipe");
\t\t\t}}
\t\t}}
\t\tif (prevfd >= 0) {{
\t\t\tsave0 = dup(0);
\t\t\tif (save0 < 0)
\t\t\t\tash_msg_and_raise_perror("can't dup stdin");
\t\t\tdup2(prevfd, 0);
\t\t\tclose(prevfd);
\t\t\tprevfd = -1;
\t\t}}
\t\tif (pip[1] >= 0) {{
\t\t\tsave1 = dup(1);
\t\t\tif (save1 < 0)
\t\t\t\tash_msg_and_raise_perror("can't dup stdout");
\t\t\tdup2(pip[1], 1);
\t\t\tclose(pip[1]);
\t\t}}
\t\tstatus = evaltree(lp->n, flags | EV_TESTED);
\t\tif (save1 >= 0) {{
\t\t\tdup2(save1, 1);
\t\t\tclose(save1);
\t\t}}
\t\tif (save0 >= 0) {{
\t\t\tdup2(save0, 0);
\t\t\tclose(save0);
\t\t}}
\t\tif (lp->next) {{
\t\t\tprevfd = pip[0];
\t\t}} else if (pip[0] >= 0) {{
\t\t\tclose(pip[0]);
\t\t}}
\t}}
\texitstatus = status;
\t(void)jp;"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_ash_inproc_pipe.py /path/to/ash.c", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARKER in text:
        print(f"[patch] already applied: {path}")
        return 0
    if OLD not in text:
        print(f"[patch] ERROR: evalpipe loop not found in {path}", file=sys.stderr)
        return 1
    path.write_text(text.replace(OLD, NEW, 1), encoding="utf-8")
    print(f"[patch] applied inproc pipe to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
