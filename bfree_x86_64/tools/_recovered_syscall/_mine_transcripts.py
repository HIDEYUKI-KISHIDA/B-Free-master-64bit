import json
import re
from collections import Counter
from pathlib import Path

root = Path(
    r"C:\Users\h_kis\.cursor\projects\c-Users-h-kis-Desktop-B-Free-master-Program-bfree-x86-64\agent-transcripts"
)
keywords = re.compile(
    r"as_copy|g_inet_socks|bfree_inet_|exec_transfer_rip|parked|bfree_coop_as_switch|"
    r"timerfd|g_unix_socks|ptmx|sigframe|futex|CLONE_THREAD|inet_socks|guest_fork_was|"
    r"bfree_guest_as_copy|g_guest_.*parked|unix_socks",
    re.I,
)
out_dir = Path("tools/_recovered_syscall/_raw_hits")
out_dir.mkdir(parents=True, exist_ok=True)

hits = []
for jf in root.rglob("*.jsonl"):
    try:
        lines = jf.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:
        continue
    for li, line in enumerate(lines):
        if "StrReplace" not in line and "Write" not in line and "new_string" not in line and "contents" not in line:
            continue
        if "syscall" not in line and not keywords.search(line):
            continue
        try:
            obj = json.loads(line)
        except Exception:
            continue

        def walk(o):
            if isinstance(o, dict):
                name = o.get("name") or o.get("toolName") or o.get("tool")
                args = o.get("arguments") or o.get("input") or o.get("params")
                if name and args is not None:
                    if isinstance(args, str):
                        try:
                            args = json.loads(args)
                        except Exception:
                            args = {"raw": args}
                    if isinstance(args, dict):
                        p = str(args.get("path", ""))
                        ns = args.get("new_string") or args.get("contents") or args.get("new_str") or ""
                        os_ = args.get("old_string") or args.get("old_str") or ""
                        if not isinstance(ns, str):
                            ns = str(ns) if ns else ""
                        if not isinstance(os_, str):
                            os_ = str(os_) if os_ else ""
                        blob = p + "\n" + ns + "\n" + os_
                        pnorm = p.replace("\\", "/")
                        is_syscall = "syscall.c" in pnorm or "sysmain/syscall" in pnorm
                        kw = bool(keywords.search(blob))
                        if is_syscall or (kw and ("syscall" in blob.lower() or is_syscall)):
                            if is_syscall or kw:
                                hits.append(
                                    {
                                        "file": str(jf),
                                        "line": li + 1,
                                        "tool": str(name),
                                        "path": p,
                                        "old_len": len(os_),
                                        "new_len": len(ns),
                                        "old_string": os_,
                                        "new_string": ns,
                                        "kw": kw,
                                        "is_syscall": is_syscall,
                                    }
                                )
                for v in o.values():
                    walk(v)
            elif isinstance(o, list):
                for v in o:
                    walk(v)

        walk(obj)

print("total hits", len(hits))
print("by tool", dict(Counter(h["tool"] for h in hits)))
kh = [h for h in hits if h["kw"] or h["is_syscall"]]
# Prefer keyword-related for recovery focus, but keep large syscall patches
kh = [h for h in hits if h["kw"]]
print("keyword hits", len(kh))
kh.sort(key=lambda x: -x["new_len"])

# Dedup by new_string hash
seen = set()
uniq = []
for h in kh:
    key = hash(h["new_string"])
    if key in seen:
        continue
    seen.add(key)
    uniq.append(h)
print("unique keyword new_strings", len(uniq))

theme_list = [
    "as_copy",
    "inet",
    "parked",
    "exec_transfer",
    "unix",
    "coop",
    "timerfd",
    "ptmx",
    "sigframe",
    "futex",
    "CLONE_THREAD",
]
idx = []
for i, h in enumerate(uniq[:300]):
    ns = h["new_string"]
    os_ = h["old_string"]
    themes = [t for t in theme_list if t.lower() in ns.lower() or t.lower() in os_.lower()]
    snip = ns[:120].replace("\n", " ") if ns else ""
    idx.append(
        {
            "i": i,
            "themes": themes,
            "new_len": h["new_len"],
            "old_len": h["old_len"],
            "tool": h["tool"],
            "path": h["path"],
            "src": Path(h["file"]).name,
            "line": h["line"],
            "snip": snip,
        }
    )
    if h["new_len"] >= 80:
        (out_dir / f"hit_{i:03d}_{Path(h['file']).stem[:8]}.json").write_text(
            json.dumps(
                {
                    "meta": {k: h[k] for k in ["file", "line", "tool", "path", "old_len", "new_len"]},
                    "themes": themes,
                    "old_string": h["old_string"],
                    "new_string": h["new_string"],
                },
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )

Path("tools/_recovered_syscall/_raw_hits/_index.json").write_text(
    json.dumps(idx, indent=2, ensure_ascii=False), encoding="utf-8"
)
print("wrote index", len(idx))
for x in idx[:40]:
    print(x["i"], x["new_len"], x["themes"], x["src"][:20], x["snip"][:70])
