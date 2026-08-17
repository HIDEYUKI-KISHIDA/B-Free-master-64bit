#!/usr/bin/env python3
"""After proven populate(), dump QQmlComponentPrivate fields only.

No beginCreate / completeCreate / loadUrl / Wayland.
On PF restore desktop.elf.g1-populate.
"""
from __future__ import annotations

import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

HELPER_START = "static void guest_g1_instantiate_from_cached_unit"
HELPER_END = "static void guest_g1_post_loop_thin_qml"

ANCHOR = """            guest_serial_puts("[desktop_qt] G1 runtimeStrings=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)exec->runtimeStrings);
            guest_serial_puts("\\n");
"""

DUMP = """            guest_serial_puts("[desktop_qt] G1 runtimeStrings=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)exec->runtimeStrings);
            guest_serial_puts("\\n");
            guest_serial_puts("[desktop_qt] G1 start=");
            guest_serial_hex_u64((uint64_t)(uint32_t)priv->start);
            guest_serial_puts("\\n");
            if (!priv->typeData)
                guest_serial_puts("[desktop_qt] G1 typeData null\\n");
            else
                guest_serial_puts("[desktop_qt] G1 typeData ok\\n");
            if (priv->url.isEmpty())
                guest_serial_puts("[desktop_qt] G1 url empty\\n");
            else
                guest_serial_puts("[desktop_qt] G1 url ok\\n");
            guest_serial_puts("[desktop_qt] G1 ctx=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)g_engine->rootContext());
            guest_serial_puts("\\n");
"""


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def nl(s: str, helper: str) -> str:
    if "\r\n" in helper:
        return s.replace("\n", "\r\n")
    return s


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    start = src.find(HELPER_START)
    end = src.find(HELPER_END)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit("instantiate helper bounds not found")
    helper = src[start:end]
    if "G1 start=" in helper:
        print("[ok] already priv dump", path)
        return
    if "G1 populate ok" not in helper:
        raise SystemExit("populate site missing; restore g1-populate source first")
    if "G1 beginCreate begin" in helper:
        raise SystemExit("beginCreate in instantiate helper; refuse")
    anchor = nl(ANCHOR, helper)
    if anchor not in helper:
        raise SystemExit("runtimeStrings print not found")
    new_helper = helper.replace(anchor, nl(DUMP, helper), 1)
    src = src[:start] + new_helper + src[end:]
    path.write_text(src, encoding="utf-8")
    print("[ok] priv dump", path)


if __name__ == "__main__":
    main()
