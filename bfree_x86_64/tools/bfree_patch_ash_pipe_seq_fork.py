#!/usr/bin/env python3
"""Make ash pipelines use sequential vfork stages for B-Free (no inproc).

Upstream evalpipe forks every stage then waitforjob(). B-Free allows only one
live cooperative child, so concurrent pipeline forks get EAGAIN. This patch:
  - forks one stage at a time (makejob(1))
  - waitforjob() after each foreground stage before the next fork
  - every pipe stage uses vfork (parent frozen in the syscall until the child
    exits/execs). AS-copy (SYS_fork) returns to the parent immediately, so the
    next stage hits EAGAIN while the first child is still live.
  - other forkshell callers also stay on vfork
  - unbuffers stdout in the pipe child so FILE-backed builtins flush

Not the inproc-pipe workaround: each stage is a real process.
KEEP_INPROC must stay 0.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER = "B-Free: seq fork pipe"
FORK_MARKER = "B-Free: AS-copy pipe fork"
FLAG_MARKER = "B-Free: pipe AS-copy flag"
SEQ_VFORK_MARKER = "B-Free: seq-fork freezes the parent in vfork"

OLD_PIPE = """\tfor (lp = n->npipe.cmdlist; lp; lp = lp->next) {
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

NEW_PIPE = f"""\t/* {MARKER}: one live coop child — fork+wait each stage. */
\tfor (lp = n->npipe.cmdlist; lp; lp = lp->next) {{
\t\tint stage_pid;
\t\tprehash(lp->n);
\t\tpip[1] = -1;
\t\tif (lp->next) {{
\t\t\tif (pipe(pip) < 0) {{
\t\t\t\tif (prevfd >= 0)
\t\t\t\t\tclose(prevfd);
\t\t\t\tash_msg_and_raise_perror("can't create pipe");
\t\t\t}}
\t\t}}
\t\tjp = makejob(/*n,*/ 1);
\t\t/* {SEQ_VFORK_MARKER} per stage.
\t\t * AS-copy for pipe builtins (vfork would trash parent AS). Seq-fork
\t\t * waits each stage so only one AS-copy child is live. */
\t\tbfree_ash_pipe_as_copy = 1;
\t\tstage_pid = forkshell(jp, lp->n, n->npipe.pipe_backgnd);
\t\tif (stage_pid == 0) {{
\t\t\t/* child */
\t\t\tbfree_ash_pipe_as_copy = 0;
\t\t\tINT_ON;
\t\t\tif (pip[1] >= 0) {{
\t\t\t\tclose(pip[0]);
\t\t\t}}
\t\t\tif (prevfd > 0) {{
\t\t\t\tdup2(prevfd, 0);
\t\t\t\tclose(prevfd);
\t\t\t}}
\t\t\tif (pip[1] > 1) {{
\t\t\t\tdup2(pip[1], 1);
\t\t\t\tclose(pip[1]);
\t\t\t}}
\t\t\t/* B-Free: unbuffered stdout so builtins reach the pipe before _exit */
\t\t\tsetvbuf(stdout, NULL, _IONBF, 0);
\t\t\tevaltreenr(lp->n, flags);
\t\t\t/* never returns */
\t\t}}
\t\tbfree_ash_pipe_as_copy = 0;
\t\t/* parent */
\t\tif (prevfd >= 0)
\t\t\tclose(prevfd);
\t\tprevfd = pip[0];
\t\tif (pip[1] != -1)
\t\t\tclose(pip[1]);
\t\tif (n->npipe.pipe_backgnd == 0) {{
\t\t\tint st;
\t\t\tINT_ON;
\t\t\t/* B-Free: wait this stage pid before next forkshell (AS-copy). */
\t\t\twhile (waitpid(stage_pid, &st, 0) < 0) {{
\t\t\t\tif (errno != EINTR)
\t\t\t\t\tbreak;
\t\t\t}}
\t\t\tstatus = waitforjob(jp);
\t\t\tINT_OFF;
\t\t\tTRACE(("evalpipe: stage done exit status %d\\n", status));
\t\t}}
\t}}"""

OLD_MAKEJOB_BLOCK = """\tjp = makejob(/*n,*/ pipelen);
\tprevfd = -1;
"""

NEW_MAKEJOB_BLOCK = """\tjp = NULL;
\tprevfd = -1;
"""


def ensure_pipe_as_copy_flag(text: str) -> str:
    """Global flag + forkshell branch: AS-copy only when evalpipe sets the flag."""
    if FLAG_MARKER not in text:
        anchor = "/* jp and n are NULL when called by openhere() for heredoc support */\n"
        decl = (
            f"/* {FLAG_MARKER}: set around evalpipe forkshell only */\n"
            "static int bfree_ash_pipe_as_copy;\n"
            "/* B-Free */ int bfree_linux_fork(void);\n"
        )
        if anchor not in text:
            raise SystemExit("[patch] ERROR: forkshell anchor not found for flag")
        text = text.replace(anchor, decl + anchor, 1)

    text = text.replace(
        "static int\n/* B-Free */ int bfree_linux_fork(void);\nforkshell(",
        "static int\nforkshell(",
    )
    text = text.replace(
        "static int\n/* B-Free */ pid_t bfree_linux_fork(void);\nforkshell(",
        "static int\nforkshell(",
    )

    idx = text.find("forkshell(struct job *jp, union node *n, int mode)")
    if idx < 0:
        raise SystemExit("[patch] ERROR: forkshell not found")
    end = idx + 700
    sub = text[idx:end]

    want = (
        f"\t/* {FORK_MARKER}: SYS_fork only for evalpipe stages */\n"
        "\tif (bfree_ash_pipe_as_copy)\n"
        "\t\tpid = bfree_linux_fork();\n"
        "\telse\n"
        "\t\tpid = vfork();"
    )

    if "bfree_ash_pipe_as_copy" in sub and "bfree_linux_fork()" in sub:
        pass
    elif "bfree_ash_pipe_as_copy" in sub:
        sub2 = re.sub(
            r"\t/\* B-Free: AS-copy pipe fork[^\n]*\*/\n"
            r"\tif \(bfree_ash_pipe_as_copy\)\n"
            r"\t\tpid = vfork\(\);\n"
            r"\telse\n"
            r"\t\tpid = vfork\(\);",
            want,
            sub,
            count=1,
        )
        if sub2 == sub:
            sub2 = re.sub(
                r"\tif \(bfree_ash_pipe_as_copy\)\n"
                r"\t\tpid = vfork\(\);\n"
                r"\telse\n"
                r"\t\tpid = vfork\(\);",
                want,
                sub,
                count=1,
            )
        if sub2 == sub:
            raise SystemExit("[patch] ERROR: cannot repair clobbered AS-copy forkshell")
        sub = sub2
        text = text[:idx] + sub + text[end:]
    else:
        if "\tpid = vfork();" not in sub and "\tpid = fork();" not in sub:
            raise SystemExit("[patch] ERROR: forkshell pid assign not found")
        old_pid = "\tpid = vfork();" if "\tpid = vfork();" in sub else "\tpid = fork();"
        sub = sub.replace(old_pid, want, 1)
        text = text[:idx] + sub + text[end:]
    return text


def refresh_seq_pipe_body(text: str) -> str:
    """Replace seq-fork body with NEW_PIPE when missing explicit stage wait."""
    start = text.find(f"\t/* {MARKER}")
    if start < 0:
        return text
    if SEQ_VFORK_MARKER in text and "stage_pid = forkshell" in text:
        old = text
        text = text.replace(
            "\t\tbfree_ash_pipe_as_copy = 0;\n"
            "\t\tif (forkshell(jp, lp->n, n->npipe.pipe_backgnd) == 0) {",
            "\t\tbfree_ash_pipe_as_copy = 1;\n"
            "\t\tif (forkshell(jp, lp->n, n->npipe.pipe_backgnd) == 0) {",
            1,
        )
        if text != old:
            print("[patch] re-enabled AS-copy for seq-fork pipe stages")
        return text
    end_marker = "\tINT_ON;\n\n\treturn status;"
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit("[patch] ERROR: cannot locate end of seq-fork evalpipe")
    text = text[:start] + NEW_PIPE + "\n" + text[end:]
    print("[patch] refreshed seq-fork pipe body (AS-copy + stage waitpid)")
    return text


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: bfree_patch_ash_pipe_seq_fork.py /path/to/ash.c", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8", errors="replace")

    if MARKER not in text:
        if OLD_PIPE not in text:
            print(f"[patch] ERROR: evalpipe loop not found in {path}", file=sys.stderr)
            return 1
        text = text.replace(OLD_PIPE, NEW_PIPE, 1)
        if OLD_MAKEJOB_BLOCK in text:
            text = text.replace(OLD_MAKEJOB_BLOCK, NEW_MAKEJOB_BLOCK, 1)
        print(f"[patch] applied seq-fork pipe to {path}")
    else:
        if "setvbuf(stdout, NULL, _IONBF, 0);" not in text:
            print(
                f"[patch] ERROR: seq-fork present but missing setvbuf; restore ash and re-run",
                file=sys.stderr,
            )
            return 1
        text = refresh_seq_pipe_body(text)
        print(f"[patch] seq-fork pipe already applied: {path}")

    text = ensure_pipe_as_copy_flag(text)
    path.write_text(text, encoding="utf-8")
    print("[patch] forkshell: AS-copy only when bfree_ash_pipe_as_copy (pipe uses vfork)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
