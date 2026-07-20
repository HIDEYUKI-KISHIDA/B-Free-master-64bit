#!/usr/bin/env python3
"""Restore: HEAD syscall.c + StrReplace replay from primary wipe-era transcripts only."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROG = ROOT.parent
SYSCALL = ROOT / "kernel" / "sysmain" / "syscall.c"
HEAD_SNAP = ROOT / "tools" / "_recovered_syscall" / "binary_bak" / "syscall.c.head_snap"
TRANS = Path(
    "/mnt/c/Users/h_kis/.cursor/projects/"
    "c-Users-h-kis-Desktop-B-Free-master-Program-bfree-x86-64/agent-transcripts"
)

# Primary conversation that built pre-wipe state + its subagents
ALLOW_PREFIXES = (
    "97982f27-2433-4380-be52-11c4d12e033b",
)

DRY = "--dry-run" in sys.argv


def collect_ops():
    ops = []
    for dirpath, _, files in os.walk(TRANS):
        for fn in files:
            if not fn.endswith(".jsonl"):
                continue
            path = Path(dirpath) / fn
            rel = str(path).replace("\\", "/")
            if not any(p in rel for p in ALLOW_PREFIXES):
                continue
            mtime = path.stat().st_mtime
            with path.open(encoding="utf-8", errors="replace") as f:
                for line_no, line in enumerate(f, 1):
                    if "StrReplace" not in line or "syscall.c" not in line:
                        continue
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    role = obj.get("role") or (obj.get("message") or {}).get("role")
                    if role != "assistant":
                        continue
                    msg = obj.get("message") or {}
                    content = msg.get("content") if isinstance(msg, dict) else None
                    if not isinstance(content, list):
                        continue
                    for part in content:
                        if not isinstance(part, dict):
                            continue
                        if part.get("name") != "StrReplace":
                            continue
                        inp = part.get("input") or {}
                        pth = (inp.get("path") or "").replace("\\", "/").lower()
                        if "sysmain/syscall.c" not in pth:
                            continue
                        old, new = inp.get("old_string"), inp.get("new_string")
                        if old is None or new is None or old == new:
                            continue
                        ops.append(
                            {
                                "mtime": mtime,
                                "line": line_no,
                                "file": str(path),
                                "old": old,
                                "new": new,
                                "replace_all": bool(inp.get("replace_all")),
                            }
                        )
    ops.sort(key=lambda o: (o["mtime"], o["line"], o["file"]))
    return ops


def main() -> int:
    if HEAD_SNAP.is_file():
        text = HEAD_SNAP.read_text(encoding="utf-8", errors="replace")
        print("baseline head_snap", len(text))
    else:
        text = subprocess.check_output(
            ["git", "show", "00891c5:bfree_x86_64/kernel/sysmain/syscall.c"],
            cwd=str(PROG),
        ).decode("utf-8", "replace")
        print("baseline git HEAD", len(text))

    ops = collect_ops()
    print("ops", len(ops))
    applied = skipped = failed = 0
    for i, op in enumerate(ops):
        old, new = op["old"], op["new"]
        if old not in text:
            if new in text:
                skipped += 1
            else:
                failed += 1
            continue
        if op["replace_all"]:
            text = text.replace(old, new)
        else:
            text = text.replace(old, new, 1)
        applied += 1
    print(f"applied={applied} skipped={skipped} failed={failed} final={len(text)}")
    if not DRY:
        SYSCALL.write_text(text, encoding="utf-8", newline="\n")
        print("wrote", SYSCALL, SYSCALL.stat().st_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
