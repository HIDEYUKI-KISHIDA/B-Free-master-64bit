# -*- coding: utf-8 -*-
import json
import re
from collections import defaultdict
from pathlib import Path

idx_path = Path("tools/_recovered_syscall/_raw_hits/_index.json")
idx = json.loads(idx_path.read_text(encoding="utf-8"))
raw = Path("tools/_recovered_syscall/_raw_hits")

# Classify each saved hit
rows = []
for p in sorted(raw.glob("hit_*.json")):
    d = json.loads(p.read_text(encoding="utf-8"))
    ns = d.get("new_string") or ""
    meta = d.get("meta") or {}
    path = (meta.get("path") or "").replace("\\", "/")
    themes = d.get("themes") or []
    is_py = ns.lstrip().startswith("#!") or "#!/usr/bin/env python" in ns[:80]
    is_syscall = "syscall.c" in path
    is_cish = (
        ("static " in ns or "long " in ns or "void " in ns or "uint64_t" in ns)
        and ("{" in ns)
        and not is_py
    )
    # key symbols
    syms = sorted(
        set(
            re.findall(
                r"\b(?:g_guest_fork_was_as_copy|bfree_guest_as_copy_\w+|bfree_inet_\w+|"
                r"g_inet_socks|g_unix_socks|g_bfree_exec_transfer_rip|"
                r"g_guest_(?:child|parent)_parked_\w+|bfree_coop_as_switch_to|"
                r"bfree_timerfd_\w+|sys_timerfd_\w+|sys_futex|g_futex_\w+|"
                r"bfree_guest_thread_\w+|CLONE_THREAD)\b",
                ns,
            )
        )
    )
    rows.append(
        {
            "file": p.name,
            "new_len": meta.get("new_len", len(ns)),
            "path": path,
            "themes": themes,
            "is_py": is_py,
            "is_syscall": is_syscall,
            "is_cish": is_cish,
            "syms": syms,
            "src": Path(meta.get("file", "")).name,
            "snip": ns[:100].replace("\n", " "),
        }
    )

# Prefer syscall.c C patches with target symbols
focus = [
    r
    for r in rows
    if r["is_syscall"] and r["is_cish"] and not r["is_py"] and (r["syms"] or any(t in r["themes"] for t in ["as_copy", "inet", "parked", "exec_transfer", "timerfd", "futex", "CLONE_THREAD", "ptmx", "sigframe"]))
]
focus.sort(key=lambda x: -x["new_len"])

out = Path("tools/_recovered_syscall/_focus_hits.json")
out.write_text(json.dumps(focus, indent=2, ensure_ascii=False), encoding="utf-8")
print("focus count", len(focus))
for r in focus[:50]:
    print(
        r["new_len"],
        r["file"],
        r["themes"],
        r["syms"][:8],
        r["snip"][:60],
    )

# Theme buckets of largest C syscall patches
buckets = defaultdict(list)
for r in focus:
    for t in r["themes"] or ["other"]:
        buckets[t].append(r["file"])

print("\n--- by theme ---")
for t, files in sorted(buckets.items()):
    print(t, len(files), "largest", files[0] if files else None)

# Also list as_copy / inet specifically even small
print("\n--- as_copy/inet/parked/exec ---")
for r in focus:
    if any(t in r["themes"] for t in ["as_copy", "inet", "parked", "exec_transfer"]) or any(
        "as_copy" in s or "inet" in s or "parked" in s or "exec_transfer" in s for s in r["syms"]
    ):
        print(r["new_len"], r["file"], r["syms"][:10], r["snip"][:70])
