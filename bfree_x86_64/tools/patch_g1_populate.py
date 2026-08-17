#!/usr/bin/env python3
"""After proven Ready, call exec->populate() only.

Qt 6.8 executableCompilationUnit() does not populate; runtimeStrings stays
null. Do not call beginCreate / completeCreate / loadUrl / Wayland.
On PF restore desktop.elf.g1-ready.
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

POPULATE = """    if (g_g1_comp->isReady()) {
        guest_serial_puts("[desktop_qt] G1 thin QML Ready\\n");
        if (!exec) {
            guest_serial_puts("[desktop_qt] G1 populate exec null\\n");
        } else {
            guest_serial_puts("[desktop_qt] G1 populate begin\\n");
            exec->populate();
            guest_serial_puts("[desktop_qt] G1 populate ok\\n");
            guest_serial_puts("[desktop_qt] G1 runtimeStrings=");
            guest_serial_hex_u64((uint64_t)(uintptr_t)exec->runtimeStrings);
            guest_serial_puts("\\n");
        }
    }
"""

OLD_COMMENT = (
    "/* Read HIT CachedQmlUnit header only. create() hung on guest; do not call it.\n"
    " * No loadUrl / TypeLoader / beginCreate / ExecutableCompilationUnit. */"
)
NEW_COMMENT = (
    "/* HIT cache unit: attach exec then populate() only. No beginCreate / loadUrl. */"
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


def patch_helper(helper: str) -> str:
    if "G1 populate begin" in helper:
        return helper
    anchor = "if (g_g1_comp->isReady())"
    err = "else if (g_g1_comp->isError())"
    i = helper.find(anchor)
    j = helper.find(err)
    if i < 0 or j < 0 or j <= i:
        raise SystemExit("instantiate Ready site not found; restore g1-ready source first")
    line_start = helper.rfind("\n", 0, i) + 1
    pop = nl(POPULATE, helper).rstrip() + " "
    return helper[:line_start] + pop + helper[j:]


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    start = src.find(HELPER_START)
    end = src.find(HELPER_END)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit("instantiate helper bounds not found")
    helper = src[start:end]
    if "G1 populate begin" in helper:
        print("[ok] already populate", path)
        return
    if "executableCompilationUnit(std::move(cu))" not in helper:
        raise SystemExit("engine wrap missing; restore g1-ready first")
    new_helper = patch_helper(helper)
    if "G1 beginCreate" in new_helper:
        raise SystemExit("beginCreate still in instantiate helper; refuse")
    src = src[:start] + new_helper + src[end:]
    src = src.replace(OLD_COMMENT, NEW_COMMENT, 1)
    path.write_text(src, encoding="utf-8")
    print("[ok] populate", path)


if __name__ == "__main__":
    main()
