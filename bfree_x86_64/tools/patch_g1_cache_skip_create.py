#!/usr/bin/env python3
"""Replace ExecutableCompilationUnit::create() with a qmlData header probe.

create() hung on the guest. Do not call it again. No loadUrl / beginCreate.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

SKIP_BODY = r'''    guest_serial_puts("[desktop_qt] G1 cache instantiate skip create\n");
    guest_serial_puts("[desktop_qt] G1 qmlData=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cached->qmlData);
    guest_serial_puts("\n");
    const quint32 *w = reinterpret_cast<const quint32 *>(cached->qmlData);
    guest_serial_puts("[desktop_qt] G1 qmlData w0=");
    guest_serial_hex_u64((uint64_t)w[0]);
    guest_serial_puts(" w1=");
    guest_serial_hex_u64((uint64_t)w[1]);
    guest_serial_puts(" w2=");
    guest_serial_hex_u64((uint64_t)w[2]);
    guest_serial_puts(" w3=");
    guest_serial_hex_u64((uint64_t)w[3]);
    guest_serial_puts("\n");
    g_g1_done = 1;
}

'''

# From first Qt object after "data ok" through the closing brace of the helper.
CREATE_TAIL_RE = re.compile(
    r"(guest_serial_puts\(\"\[desktop_qt\] G1 cache instantiate data ok\\n\"\);\r?\n)"
    r"(?:.*?)(    g_g1_done = 1;\r?\n\})",
    re.S,
)


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    if "G1 cache instantiate skip create" in src:
        print("[ok] already skip create", path)
        return
    if "ExecutableCompilationUnit::create" not in src and "unique_ptr<QV4::CompiledData::CompilationUnit>" not in src:
        raise SystemExit("create() site not found; restore good-running first")
    m = CREATE_TAIL_RE.search(src)
    if not m:
        raise SystemExit("instantiate tail not found")
    src = src[: m.start(1)] + m.group(1) + SKIP_BODY + src[m.end() :]
    path.write_text(src, encoding="utf-8")
    print("[ok] skip create", path)


if __name__ == "__main__":
    main()
