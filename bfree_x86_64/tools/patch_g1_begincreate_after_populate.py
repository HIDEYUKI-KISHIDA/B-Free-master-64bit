#!/usr/bin/env python3
"""After proven populate(), call beginCreate only.

Do not call completeCreate / loadUrl / Wayland.
Do not run this on Ready-only (g1-ready). On PF restore g1-populate.
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

BEGINCREATE = """            guest_serial_puts("[desktop_qt] G1 runtimeStrings=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)exec->runtimeStrings);
            guest_serial_puts("\\n");
            if (!exec->runtimeStrings) {
                guest_serial_puts("[desktop_qt] G1 beginCreate skip (no runtimeStrings)\\n");
            } else {
                guest_serial_puts("[desktop_qt] G1 beginCreate begin\\n");
                QObject *obj = g_g1_comp->beginCreate(g_engine->rootContext());
                guest_serial_puts("[desktop_qt] G1 beginCreate end\\n");
                if (!obj)
                    guest_serial_puts("[desktop_qt] G1 beginCreate null\\n");
                else {
                    guest_serial_puts("[desktop_qt] G1 beginCreate obj=");
                    guest_serial_hex_u64((uint64_t)(uintptr_t)obj);
                    guest_serial_puts("\\n");
                }
            }
"""

NEW_COMMENT = (
    "/* HIT cache unit: populate() then beginCreate only. No completeCreate / loadUrl. */"
)
OLD_COMMENTS = (
    "/* HIT cache unit: attach exec then populate() only. No beginCreate / loadUrl. */",
    "/* Read HIT CachedQmlUnit header only. create() hung on guest; do not call it.\n"
    " * No loadUrl / TypeLoader / beginCreate / ExecutableCompilationUnit. */",
)


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
    if "G1 beginCreate begin" in helper:
        print("[ok] already beginCreate after populate", path)
        return
    if "G1 populate ok" not in helper:
        raise SystemExit("populate site missing; restore g1-populate source first")
    if "completeCreate" in helper:
        raise SystemExit("completeCreate in instantiate helper; refuse")
    anchor = nl(ANCHOR, helper)
    if anchor not in helper:
        raise SystemExit("runtimeStrings print not found")
    new_helper = helper.replace(anchor, nl(BEGINCREATE, helper), 1)
    if "G1 populate ok" not in new_helper:
        raise SystemExit("populate lost; refuse")
    src = src[:start] + new_helper + src[end:]
    for old in OLD_COMMENTS:
        src = src.replace(old, NEW_COMMENT, 1)
    path.write_text(src, encoding="utf-8")
    print("[ok] beginCreate after populate", path)


if __name__ == "__main__":
    main()
