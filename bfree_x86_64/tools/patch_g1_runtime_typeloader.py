#!/usr/bin/env python3
"""Patch local guest_main.cpp: after proven G1 cache HIT, set the
post-loop typeloader flag and PreferSynchronous loadUrl.

Does not touch the boot DesktopShell skip. Safe to re-run.
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


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def serial_fn(src: str) -> str:
    return "guest_serial_write" if src.count("guest_serial_write(") > src.count("guest_serial_puts(") else "guest_serial_puts"


def ensure_extern(src: str) -> str:
    if "bfree_guest_set_typeloader_main_ok" in src:
        return src
    needle = "void bfree_guest_qt_coop_schedule(void);"
    add = (
        needle
        + "\nvoid bfree_guest_set_typeloader_main_ok(int on);\n"
        + "int bfree_guest_typeloader_main_ok(void);"
    )
    if needle in src:
        return src.replace(needle, add, 1)
    raise SystemExit("extern block marker not found")


def patch_g1(src: str, fn: str) -> str:
    if "G1 sync loadUrl begin" in src and "bfree_guest_set_typeloader_main_ok(1)" in src:
        return src
    insert = (
        f"    bfree_guest_set_typeloader_main_ok(1);\n"
        f"    {fn}(\"[desktop_qt] G1 sync loadUrl begin\\n\");\n"
        f"    g_g1_comp->loadUrl(QUrl(QStringLiteral(\"qrc:/GuestGate1Window.qml\")),\n"
        f"                       QQmlComponent::PreferSynchronous);\n"
        f"    {fn}(\"[desktop_qt] G1 sync loadUrl end\\n\");\n"
    )
    # HIT-only local tree: stop returning after cache unit ok.
    old_done = (
        f'    {fn}("[desktop_qt] G1 cache unit ok\\n");\n'
        "    g_g1_done = 1;\n"
        "    return;\n"
    )
    new_done = f'    {fn}("[desktop_qt] G1 cache unit ok\\n");\n' + insert
    if old_done in src:
        return src.replace(old_done, new_done, 1)
    marker = f'    {fn}("[desktop_qt] G1 cache unit ok\\n");\n'
    if marker in src and "G1 sync loadUrl begin" not in src:
        return src.replace(marker, marker + insert, 1)
    raise SystemExit("G1 cache unit ok site not found; not patching blindly")


COMPAT_CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/tools/guest_link_compat.cpp"),
    Path("/workspace/bfree_x86_64/tools/guest_link_compat.cpp"),
    Path.cwd() / "tools/guest_link_compat.cpp",
    Path.cwd() / "bfree_x86_64/tools/guest_link_compat.cpp",
]

FLAG_IMPL = """
/* Gate 1: stock isThisThread() until the event loop. Never true during STAGE 5 qrc. */
static int g_bfree_typeloader_main_ok;

extern "C" void bfree_guest_set_typeloader_main_ok(int on)
{
    g_bfree_typeloader_main_ok = on ? 1 : 0;
    bfree_guest_serial_lit("[desktop_qt] typeloader main ok=");
    bfree_guest_serial_lit(on ? "1\\n" : "0\\n");
}

extern "C" int bfree_guest_typeloader_main_ok(void)
{
    return g_bfree_typeloader_main_ok;
}
"""


def patch_compat() -> None:
    extra = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    path = None
    for p in ([extra] if extra else []) + COMPAT_CANDIDATES:
        if p and p.is_file():
            path = p
            break
    if path is None:
        raise SystemExit("guest_link_compat.cpp not found")
    src = path.read_text(encoding="utf-8", errors="replace")
    if "bfree_guest_set_typeloader_main_ok" in src:
        print("[ok] compat already has flag", path)
        return
    needle = "extern \"C\" void bfree_guest_qresource_sanitize(void)"
    idx = src.find(needle)
    if idx < 0:
        raise SystemExit("compat sanitize marker not found")
    # insert after the sanitize function body
    end = src.find("}", idx)
    if end < 0:
        raise SystemExit("compat sanitize body not found")
    end = src.find("\n", end)
    src = src[: end + 1] + FLAG_IMPL + src[end + 1 :]
    path.write_text(src, encoding="utf-8")
    print("[ok] compat", path)


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    fn = serial_fn(src)
    src = ensure_extern(src)
    src = patch_g1(src, fn)
    path.write_text(src, encoding="utf-8")
    print("[ok]", path)
    patch_compat()


if __name__ == "__main__":
    main()
