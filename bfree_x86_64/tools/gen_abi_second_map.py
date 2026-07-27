#!/usr/bin/env python3
"""Generate BusyBox×registry cross-check for the ABI second map."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUSYBOX = ROOT / "guest/rootfs/bin/busybox"
DEFAULT_DISPATCH = ROOT / "kernel/sysmain/syscall_dispatch.c"

# Registered but known-thin (Cat2 / M17 notes).
THIN = {
    13, 14, 15, 16, 35, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 165, 166, 202,
}

# Explicit non-goal NRs (io_uring/aio, namespaces/landlock/new mount, bpf/seccomp…).
NONGOAL = set(range(206, 211)) | {
    101, 155, 272, 298, 308, 312, 317, 321, 323, 333, 336, 337, 338, 339, 340, 341,
    342, 343, 344, 353, 355, 356, 357,
}

# Curated musl static CRT + common libc surface (x86_64).
MUSL_CORE = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 21, 22, 23, 28,
    39, 56, 57, 59, 60, 61, 63, 72, 79, 80, 89, 95, 96, 97, 102, 104, 107, 108,
    110, 131, 157, 158, 186, 202, 218, 228, 230, 231, 234, 257, 262, 273, 291,
    292, 293, 302, 318, 332,
}


def load_names() -> dict[int, str]:
    # Minimal names used in reports; unknown → nr_N
    return {}


def extract_busybox_nrs(path: Path) -> set[int]:
    out = subprocess.check_output(
        ["objdump", "-d", "-M", "intel", str(path)],
        text=True,
        errors="ignore",
    )
    lines = out.splitlines()
    nrs: set[int] = set()
    for i, line in enumerate(lines):
        if not re.search(r"\bsyscall\b", line):
            continue
        for j in range(max(0, i - 12), i):
            m = re.search(r"mov\s+e?ax,\s*(0x[0-9a-fA-F]+|\d+)", lines[j])
            if not m:
                continue
            raw = m.group(1)
            n = int(raw, 16) if raw.startswith("0x") else int(raw)
            if 0 <= n < 450:
                nrs.add(n)
    return nrs


def load_registry(path: Path) -> set[int]:
    text = path.read_text()
    return set(map(int, re.findall(r"bfree_syscall_implemented\[(\d+)\]", text)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--busybox", type=Path, default=DEFAULT_BUSYBOX)
    ap.add_argument("--dispatch", type=Path, default=DEFAULT_DISPATCH)
    ap.add_argument("--json", type=Path, help="optional JSON output path")
    args = ap.parse_args()

    if not args.busybox.is_file():
        print(f"missing busybox: {args.busybox}", file=sys.stderr)
        return 1
    if not args.dispatch.is_file():
        print(f"missing dispatch: {args.dispatch}", file=sys.stderr)
        return 1

    bb = extract_busybox_nrs(args.busybox)
    regs = load_registry(args.dispatch)

    bb_ok = sorted(n for n in bb if n in regs and n not in THIN)
    bb_thin = sorted(n for n in bb if n in regs and n in THIN)
    bb_miss = sorted(n for n in bb if n not in regs)
    bb_actionable = [n for n in bb_miss if n not in NONGOAL]
    bb_nongoal = [n for n in bb_miss if n in NONGOAL]
    musl_miss = sorted(n for n in MUSL_CORE if n not in regs)
    musl_thin = sorted(n for n in MUSL_CORE if n in THIN)

    report = {
        "busybox_path": str(args.busybox),
        "busybox_nr_count": len(bb),
        "busybox_registered_ok": bb_ok,
        "busybox_registered_thin": bb_thin,
        "busybox_enosys_actionable": bb_actionable,
        "busybox_enosys_nongoal": bb_nongoal,
        "musl_core_enosys": musl_miss,
        "musl_core_thin": musl_thin,
        "summary": {
            "bb_ok": len(bb_ok),
            "bb_thin": len(bb_thin),
            "bb_enosys_actionable": len(bb_actionable),
            "bb_enosys_nongoal": len(bb_nongoal),
            "musl_enosys": len(musl_miss),
            "musl_thin": len(musl_thin),
        },
    }

    print("ABI second-map evidence (BusyBox × registry)")
    print(f"  BusyBox NRs:              {len(bb)}")
    print(f"  registered (not thin):    {len(bb_ok)}")
    print(f"  registered THIN:          {len(bb_thin)} -> {bb_thin}")
    print(f"  ENOSYS actionable:        {len(bb_actionable)} -> {bb_actionable}")
    print(f"  ENOSYS non-goal:          {len(bb_nongoal)} -> {bb_nongoal}")
    print(f"  musl-core ENOSYS:         {musl_miss}")
    print(f"  musl-core THIN:           {musl_thin}")

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
