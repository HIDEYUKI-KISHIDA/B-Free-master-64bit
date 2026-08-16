#!/usr/bin/env python3
"""Ensure skip-create helper exists AND is called after G1 HIT.

Does not call ExecutableCompilationUnit::create(). Does not loadUrl.
Local trees may print qmlcache HIT GuestGate1Window and/or G1 cache unit ok.
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

HELPER = r'''
static void guest_g1_instantiate_from_cached_unit(const void *unit_raw)
{
    guest_serial_puts("[desktop_qt] G1 cache instantiate enter\n");
    if (!g_g1_comp || !g_engine || !unit_raw) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate args null\n");
        g_g1_done = 1;
        return;
    }
    const auto *cached = static_cast<const QQmlPrivate::CachedQmlUnit *>(unit_raw);
    if (!cached->qmlData) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate qmlData null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache instantiate data ok\n");
    guest_serial_puts("[desktop_qt] G1 cache instantiate skip create\n");
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

HIT_MARKERS = (
    "qmlcache HIT GuestGate1Window",
    "G1 cache unit ok",
)


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def insert_helper(src: str) -> str:
    if "guest_g1_instantiate_from_cached_unit" in src:
        return src
    for mark in (
        "static void guest_g1_post_loop_thin_qml(void)",
        "static void guest_g1_post_loop_thin_qml()",
    ):
        if mark in src:
            return src.replace(mark, HELPER + mark, 1)
    raise SystemExit("G1 function not found; cannot insert helper")


def arg_name(window: str) -> str:
    if "g1u" in window:
        return "g1u"
    if "unit" in window:
        return "unit"
    return "g1u"


def wire_call(src: str) -> str:
    if "guest_g1_instantiate_from_cached_unit(" in src.split("guest_g1_post_loop_thin_qml", 1)[-1]:
        return src
    lines = src.splitlines(keepends=True)
    out = []
    i = 0
    wired = False
    while i < len(lines):
        line = lines[i]
        out.append(line)
        if (not wired) and any(m in line for m in HIT_MARKERS):
            window = "".join(lines[max(0, i - 8) : i + 12])
            if "guest_g1_instantiate_from_cached_unit" in "".join(lines[i : i + 12]):
                i += 1
                continue
            name = arg_name(window)
            j = i + 1
            while j < len(lines) and j <= i + 12:
                s = lines[j].strip()
                if s in ("g_g1_done = 1;", "return;"):
                    j += 1
                    continue
                break
            out.append(f"        guest_g1_instantiate_from_cached_unit({name});\n")
            i = j
            wired = True
            continue
        i += 1
    if not wired:
        raise SystemExit("HIT/unit-ok site not found; cannot wire call")
    return "".join(out)


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    src = insert_helper(src)
    src = wire_call(src)
    if "ExecutableCompilationUnit::create" in src:
        raise SystemExit("create() still present; refuse")
    path.write_text(src, encoding="utf-8")
    has_helper = "G1 cache instantiate skip create" in src
    has_call = "guest_g1_instantiate_from_cached_unit(" in src
    print(f"[ok] helper={int(has_helper)} call={int(has_call)} {path}")
    if not (has_helper and has_call):
        raise SystemExit("wire incomplete")


if __name__ == "__main__":
    main()
