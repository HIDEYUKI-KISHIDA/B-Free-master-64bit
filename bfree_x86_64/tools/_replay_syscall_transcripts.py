#!/usr/bin/env python3
"""Replay transcript StrReplace ops onto syscall.c in chronological order."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYSCALL = ROOT / "kernel" / "sysmain" / "syscall.c"
TRANSCRIPTS = Path(
    "/mnt/c/Users/h_kis/.cursor/projects/"
    "c-Users-h-kis-Desktop-B-Free-master-Program-bfree-x86-64/agent-transcripts"
)
# Also accept Windows path when running under native Python
if not TRANSCRIPTS.is_dir():
    TRANSCRIPTS = Path(
        r"C:\Users\h_kis\.cursor\projects"
        r"\c-Users-h-kis-Desktop-B-Free-master-Program-bfree-x86-64"
        r"\agent-transcripts"
    )

DRY = "--dry-run" in sys.argv
VERBOSE = "-v" in sys.argv


def iter_jsonl(path: Path):
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield line_no, json.loads(line)
            except json.JSONDecodeError:
                continue


def collect_ops():
    ops = []
    for dirpath, _, files in os.walk(TRANSCRIPTS):
        for fn in files:
            if not fn.endswith(".jsonl"):
                continue
            path = Path(dirpath) / fn
            mtime = path.stat().st_mtime
            for line_no, obj in iter_jsonl(path):
                # Cursor jsonl: {"role":"assistant","message":{"content":[...]}}
                role = obj.get("role") or (obj.get("message") or {}).get("role")
                if role != "assistant":
                    continue
                msg = obj.get("message") or {}
                content = msg.get("content") if isinstance(msg, dict) else None
                if content is None:
                    content = obj.get("content")
                if not isinstance(content, list):
                    continue
                for part in content:
                    if not isinstance(part, dict):
                        continue
                    # type may be tool_use or omitted in some exports
                    name = part.get("name")
                    if name not in ("StrReplace", "Write"):
                        continue
                    if part.get("type") not in (None, "tool_use"):
                        continue
                    inp = part.get("input") or {}
                    pth = (inp.get("path") or "").replace("\\", "/").lower()
                    # Transcripts use Desktop or J: paths; accept any .../sysmain/syscall.c
                    if "sysmain/syscall.c" not in pth:
                        continue
                    if name == "StrReplace":
                        old = inp.get("old_string")
                        new = inp.get("new_string")
                        if old is None or new is None:
                            continue
                        ops.append(
                            {
                                "kind": "StrReplace",
                                "mtime": mtime,
                                "line": line_no,
                                "file": str(path),
                                "old": old,
                                "new": new,
                                "replace_all": bool(inp.get("replace_all")),
                            }
                        )
                    elif name == "Write":
                        contents = inp.get("contents")
                        if contents is None:
                            continue
                        ops.append(
                            {
                                "kind": "Write",
                                "mtime": mtime,
                                "line": line_no,
                                "file": str(path),
                                "contents": contents,
                            }
                        )
    # Stable chronological: file mtime then line number within file
    ops.sort(key=lambda o: (o["mtime"], o["line"], o["file"]))
    return ops


def main() -> int:
    text = SYSCALL.read_text(encoding="utf-8", errors="replace")
    ops = collect_ops()
    print(f"collected {len(ops)} ops targeting syscall.c")
    applied = 0
    skipped = 0
    failed = 0
    writes = 0
    for i, op in enumerate(ops):
        if op["kind"] == "Write":
            # Only accept Write if it looks like a full file (>= 100KB)
            c = op["contents"]
            if len(c) < 100000:
                skipped += 1
                if VERBOSE:
                    print(f"[{i}] skip short Write len={len(c)}")
                continue
            if not DRY:
                text = c
            writes += 1
            applied += 1
            print(f"[{i}] Write FULL len={len(c)} from {op['file']}:{op['line']}")
            continue

        old, new = op["old"], op["new"]
        if old == new:
            skipped += 1
            continue
        if old not in text:
            # Already applied / needle drifted
            if new in text:
                skipped += 1
                if VERBOSE:
                    print(f"[{i}] skip already-applied")
            else:
                failed += 1
                if VERBOSE or failed <= 40:
                    print(
                        f"[{i}] FAIL missing old ({len(old)} bytes) "
                        f"{op['file']}:{op['line']}"
                    )
            continue
        count = text.count(old)
        if op["replace_all"]:
            text = text.replace(old, new)
            applied += 1
            print(f"[{i}] OK replace_all x{count}")
        else:
            if count > 1 and VERBOSE:
                print(f"[{i}] warn: {count} matches, replacing first")
            text = text.replace(old, new, 1)
            applied += 1
            if VERBOSE:
                print(f"[{i}] OK")
    print(
        f"done applied={applied} skipped={skipped} failed={failed} "
        f"writes={writes} final_len={len(text)}"
    )
    if not DRY:
        bak = SYSCALL.with_suffix(".c.pre_replay")
        if not bak.exists():
            bak.write_text(
                SYSCALL.read_text(encoding="utf-8", errors="replace"),
                encoding="utf-8",
            )
            print(f"backup {bak}")
        SYSCALL.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {SYSCALL} ({SYSCALL.stat().st_size} bytes)")
    return 0 if failed < applied or applied > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
