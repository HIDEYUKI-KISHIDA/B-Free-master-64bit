#!/usr/bin/env python3
"""Accurate work-unit inventory: guest Linux ABI + ENOSYS stub files."""
from __future__ import annotations

import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent

# --- Guest Linux ABI ---
sc = (root / "kernel/sysmain/syscall.c").read_text(encoding="utf-8", errors="replace")
cases = sorted({int(m.group(1)) for m in re.finditer(r"\bcase\s+(\d+)\s*:", sc)})
enosys_sites = []
for i, line in enumerate(sc.splitlines(), 1):
    if re.search(r"return\s+-38\b", line) or "ENOSYS" in line:
        enosys_sites.append(i)

handlers = sorted(set(re.findall(r"static long (sys_linux_\w+)\(", sc)))
guest_names = {h.replace("sys_linux_", "", 1) for h in handlers}

# --- Policy ---
policy: set[str] = set()
for raw in (root / "POSIX_685_POLICY_ENOSYS.txt").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    s = raw.strip()
    if s and not s.startswith("#") and re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", s):
        policy.add(s)

# --- Stub files ---
stubs: list[Path] = []
for p in (root / "userland/libc").rglob("*.c"):
    t = p.read_text(encoding="utf-8", errors="replace")
    if "ENOSYS" in t and re.search(r"return\s+-1\b", t):
        stubs.append(p)

func_re = re.compile(
    r"^(?:int|long|ssize_t|void\*|char\*|unsigned|size_t|off_t|mode_t|"
    r"pid_t|uid_t|gid_t|int32_t|uint32_t)\s+(?:volatile\s+)?"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.M,
)

all_funcs: set[str] = set()
file_count_funcs: dict[str, list[str]] = {}
for p in stubs:
    t = p.read_text(encoding="utf-8", errors="replace")
    fs = set(func_re.findall(t))
    fs = {
        f
        for f in fs
        if not f.startswith("__")
        and f not in {"if", "for", "while", "switch", "return", "sizeof"}
    }
    rel = str(p.relative_to(root / "userland/libc"))
    file_count_funcs[rel] = sorted(fs)
    all_funcs |= fs

in_policy = sorted(all_funcs & policy)
not_policy = sorted(all_funcs - policy)

vfs_kw = (
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
    "fstat",
    "lstat",
    "faccess",
    "fchmod",
    "fchown",
)
sock_kw = ("socket", "bind", "connect", "listen", "accept", "poll")
sched_kw = ("sched_", "getrlimit", "readv", "timerfd")

vfs, sock, sched, other = [], [], [], []
for f in not_policy:
    fl = f.lower()
    if any(k in fl for k in vfs_kw):
        vfs.append(f)
    elif any(k in fl for k in sock_kw):
        sock.append(f)
    elif any(k in fl for k in sched_kw) or f in {"process"}:
        sched.append(f)
    else:
        other.append(f)

# guest overlap: stub basename / func already has guest handler
guest_covered_funcs = []
for f in not_policy:
    if f in guest_names or f.rstrip("at") in guest_names:
        guest_covered_funcs.append(f)
    elif f in {
        "mkdir",
        "rmdir",
        "unlink",
        "rename",
        "symlink",
        "readlink",
        "lstat",
        "fstatat",
        "utimensat",
        "fchmodat",
        "fchownat",
        "linkat",
        "unlinkat",
        "renameat",
        "mknodat",
        "faccessat",
    }:
        # guest has path ops under different names
        guest_covered_funcs.append(f)

guest_covered_funcs = sorted(set(guest_covered_funcs))
still_needed = sorted(set(not_policy) - set(guest_covered_funcs))

LINUX_X86_64_SYSCALLS_APPROX = 462  # kernel ~6.x unistd_64.h ballpark

out = root / ".cache" / "WORK_UNITS_ACCURATE_20260718.md"
lines = []
lines.append("# Accurate work-unit count (2026-07-18)\n")
lines.append("## Guest Linux ABI (`kernel/sysmain/syscall.c`)\n")
lines.append(f"| Metric | Count |")
lines.append(f"|--------|------:|")
lines.append(f"| Dispatched `case N:` (unique) | {len(cases)} |")
lines.append(f"| `sys_linux_*` handlers | {len(handlers)} |")
lines.append(f"| Lines mentioning ENOSYS / `return -38` | {len(enosys_sites)} |")
lines.append(
    f"| Approx undeclared Linux syscalls ({LINUX_X86_64_SYSCALLS_APPROX}−dispatched) | {LINUX_X86_64_SYSCALLS_APPROX - len(cases)} |"
)
lines.append("")
lines.append(
    "Note: undeclared ≠ must-implement. BusyBox/Qt need a **subset** of the gap.\n"
)

lines.append("## Stub layer (`userland/libc`, ENOSYS + `return -1`)\n")
lines.append(f"| Metric | Count |")
lines.append(f"|--------|------:|")
lines.append(f"| Stub **files** | {len(stubs)} |")
lines.append(f"| Functions defined in those files (heuristic) | {len(all_funcs)} |")
lines.append(f"| └ overlap Policy ENOSYS (skip / intentional) | {len(in_policy)} |")
lines.append(f"| └ **non-policy functions** | {len(not_policy)} |")
lines.append(f"| &nbsp;&nbsp;&nbsp;VFS-ish | {len(vfs)} |")
lines.append(f"| &nbsp;&nbsp;&nbsp;socket-ish | {len(sock)} |")
lines.append(f"| &nbsp;&nbsp;&nbsp;sched/misc | {len(sched)} |")
lines.append(f"| &nbsp;&nbsp;&nbsp;other | {len(other)} |")
lines.append(
    f"| Non-policy already mirrored on guest path (name overlap) | {len(guest_covered_funcs)} |"
)
lines.append(
    f"| Non-policy **still primarily host/bfree_posix debt** | {len(still_needed)} |"
)
lines.append("")

lines.append("### Non-policy function list\n")
for f in not_policy:
    tag = " [guest-overlap]" if f in guest_covered_funcs else ""
    lines.append(f"- `{f}`{tag}")
lines.append("")
lines.append("### Policy overlap in stubs (do not count as new work)\n")
for f in in_policy:
    lines.append(f"- `{f}`")
lines.append("")

lines.append("## Recommended work units (for planning)\n")
lines.append("| Unit | What | Count | Effort band |")
lines.append("|------|------|------:|-------------|")
lines.append(
    f"| U1 | Guest: fix/extend **known ENOSYS call sites** | {len(enosys_sites)} sites | days–2 weeks |"
)
lines.append(
    f"| U2 | Guest: **practical** missing syscalls for BusyBox→musl→Qt (not all {LINUX_X86_64_SYSCALLS_APPROX - len(cases)}) | **~40–80** (estimate) | weeks–months |"
)
lines.append(
    f"| U3 | Host/bfree_posix: non-policy stubs to real path | **{len(not_policy)} funcs / {len(stubs)} files** | weeks (less if only guest matters) |"
)
lines.append(
    f"| U4 | Policy stubs | {len(in_policy)} funcs | **0** (intentional) |"
)
lines.append("")
lines.append("### One-line totals for the user\n")
lines.append(
    f"- **If guest-first (Qt/BusyBox):** plan **~{len(enosys_sites)} known holes + ~40–80 syscalls**, "
    f"not {LINUX_X86_64_SYSCALLS_APPROX - len(cases)}.\n"
)
lines.append(
    f"- **If also clearing bfree_posix stubs:** add **{len(not_policy)} functions** "
    f"({len(stubs)} files; {len(in_policy)} policy funcs excluded).\n"
)
lines.append(
    f"- **Do not add:** Policy **{len(policy)}** intentional ENOSYS symbols as required work.\n"
)

text = "\n".join(lines)
out.write_text(text, encoding="utf-8")
print(text)
print(f"\nWrote {out}")
