#!/usr/bin/env python3
"""Recount stub work as 1 bfree_posix/musl file ≈ 1 POSIX API unit."""
from __future__ import annotations

import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sc = (root / "kernel/sysmain/syscall.c").read_text(encoding="utf-8", errors="replace")
cases = sorted({int(m.group(1)) for m in re.finditer(r"\bcase\s+(\d+)\s*:", sc)})
enosys = sum(1 for l in sc.splitlines() if re.search(r"return\s+-38\b", l))
handlers = len(set(re.findall(r"static long (sys_linux_\w+)\(", sc)))
gh = set(re.findall(r"sys_linux_(\w+)", sc))

policy: set[str] = set()
for raw in (root / "POSIX_685_POLICY_ENOSYS.txt").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    s = raw.strip()
    if s and not s.startswith("#") and re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", s):
        policy.add(s)

stubs: list[Path] = []
for p in (root / "userland/libc").rglob("*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "ENOSYS" in t and re.search(r"return\s+-1\b", t):
        stubs.append(p)

api: dict[str, str] = {}
for p in stubs:
    rel = str(p.relative_to(root))
    stem = p.stem
    if stem in ("API_NAME", "libc_common", "bfree_demo_stubs"):
        continue
    if p.parent.name == "bfree_posix":
        api[stem] = rel
        continue
    if stem.startswith("musl_libc_"):
        name = stem[len("musl_libc_") :]
        name = name.removesuffix("_stubs").removesuffix("_stub")
        api[name] = rel

names = sorted(api)
in_pol = [n for n in names if n in policy]
out_pol = [n for n in names if n not in policy]

# Guest already implements similar path ops
guest_map = {
    "mkdir": "mkdir",
    "rmdir": "rmdir",
    "unlink": "unlink",
    "rename": "rename",
    "symlink": "symlink",
    "readlink": "readlink",
    "utimensat": "utimensat",
    "fstatat": "newfstatat",
    "lstat": "newfstatat",
    "renameat": "rename",
    "unlinkat": "unlink",
    "linkat": "symlink",
    "mknodat": "mkdir",
    "faccessat": "access",
    "access": "access",
    "getrlimit": "getrlimit",
    "socketpair": "pipe",
}
guest_overlap = []
need = []
for n in out_pol:
    m = guest_map.get(n)
    if (m and m in gh) or n in gh:
        guest_overlap.append(n)
    else:
        need.append(n)

vfs = [
    n
    for n in need
    if any(
        k in n
        for k in (
            "chmod",
            "chown",
            "mkdir",
            "mknod",
            "rmdir",
            "unlink",
            "rename",
            "link",
            "symlink",
            "readlink",
            "stat",
            "access",
            "utime",
            "chroot",
        )
    )
]
sock = [
    n
    for n in need
    if any(k in n for k in ("socket", "bind", "connect", "listen", "accept", "poll"))
]
other = [n for n in need if n not in vfs and n not in sock]

lines = [
    "# Accurate planning counts (refined)",
    "",
    "## Guest Linux ABI",
    f"- Dispatched syscalls (`case N`): **{len(cases)}**",
    f"- `sys_linux_*` handlers: **{handlers}**",
    f"- Explicit `return -38` sites: **{enosys}**",
    f"- Undeclared vs ~462 Linux numbers: **{462 - len(cases)}** (not all required)",
    f"- Practical gap for BusyBox→musl→Qt: **~40–80** (estimate, not measured per-app yet)",
    "",
    "## Stub APIs (1 stub file ≈ 1 API unit; internals excluded)",
    f"- Raw stub files (ENOSYS+return -1): **{len(stubs)}**",
    f"- Countable API units: **{len(names)}**",
    f"- Policy (skip): **{len(in_pol)}** → {in_pol}",
    f"- Actionable APIs: **{len(out_pol)}**",
    f"- Already similar on guest path: **{len(guest_overlap)}** → {guest_overlap}",
    f"- Remaining stub debt: **{len(need)}**",
    f"  - VFS-ish: **{len(vfs)}** → {vfs}",
    f"  - socket-ish: **{len(sock)}** → {sock}",
    f"  - other: **{len(other)}** → {other}",
    "",
    "## Bottom line (use these numbers)",
    "",
    "| Track | Exact / best count | Notes |",
    "|-------|-------------------:|-------|",
    f"| Guest known ENOSYS sites | **{enosys}** | small, concrete |",
    f"| Guest practical syscall gap | **~40–80** | estimate until app traces |",
    f"| Guest full undeclared | **{462 - len(cases)}** | do **not** plan this as backlog |",
    f"| Stub files (raw) | **{len(stubs)}** | includes noise files |",
    f"| Stub actionable APIs | **{len(out_pol)}** | after dropping policy |",
    f"| Stub after guest credit | **{len(need)}** | real remaining if guest-first |",
    f"| Policy 94 | **{len(policy)}** | not in workload |",
    "",
    f"**Guest-first total to plan:** `{enosys} known + ~40–80 syscalls` "
    f"(optionally + **{len(need)}** host stub APIs if that path still matters).",
    "",
]
text = "\n".join(lines)
out = root / ".cache" / "WORK_UNITS_REFINED_20260718.md"
out.write_text(text, encoding="utf-8")
print(text)
print(f"Wrote {out}")
