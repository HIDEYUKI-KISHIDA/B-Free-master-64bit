#!/usr/bin/env python3
"""Insert post-event-loop Gate1 thin QML load into guest_main.cpp.

Safe to re-run. Does not touch the boot skip / goto already in local trees.
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


def serial_fn(src: str) -> str:
    return "guest_serial_write" if src.count("guest_serial_write(") > src.count("guest_serial_puts(") else "guest_serial_puts"


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def funcs(fn: str) -> str:
    return f'''
/* Wayland/GPU gate 1: QML IR Ready after the event loop, never on boot. */
static QQmlComponent *g_g1_comp = nullptr;
static int g_g1_posted = 0;
static int g_g1_done = 0;
static unsigned g_g1_polls = 0;

static void guest_g1_post_loop_thin_qml(void)
{{
    if (g_g1_posted || !g_engine)
        return;
    g_g1_posted = 1;
    {fn}("[desktop_qt] G1 post-loop thin QML begin\\n");
    {fn}("[desktop_qt] G1 empty ctor begin\\n");
    g_g1_comp = new QQmlComponent(g_engine);
    {fn}("[desktop_qt] G1 empty ctor ok\\n");
    g_g1_comp->loadUrl(QUrl(QStringLiteral("qrc:/GuestGate1Window.qml")),
                       QQmlComponent::Asynchronous);
    {fn}("[desktop_qt] G1 loadUrl async posted\\n");
    {fn}("[desktop_qt] G1 pump enter\\n");
    for (int spin = 0; g_g1_comp->isLoading() && spin < 16; ++spin) {{
        {fn}("[desktop_qt] G1 pump spin=");
        guest_serial_hex_u64((uint64_t)(unsigned)spin);
        {fn}("\\n");
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 2);
        {fn}("[desktop_qt] G1 pump ok\\n");
    }}
    {fn}("[desktop_qt] G1 pump done status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    {fn}("\\n");
}}

static void guest_g1_post_loop_thin_qml_poll(void)
{{
    if (g_g1_done || !g_g1_comp)
        return;
    ++g_g1_polls;
    if (g_g1_comp->isLoading()) {{
        if (g_g1_polls == 1u || (g_g1_polls % 10000u) == 0u) {{
            {fn}("[desktop_qt] G1 IR status=");
            guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
            {fn}(" polls=");
            guest_serial_hex_u64((uint64_t)g_g1_polls);
            {fn}("\\n");
        }}
        if (g_g1_polls >= 200000u) {{
            {fn}("[desktop_qt] G1 thin QML timeout (still Loading)\\n");
            g_g1_done = 1;
        }}
        return;
    }}
    g_g1_done = 1;
    {fn}("[desktop_qt] G1 IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    {fn}("\\n");
    if (g_g1_comp->isError()) {{
        {fn}("[desktop_qt] G1 thin QML error\\n");
        return;
    }}
    if (!g_g1_comp->isReady()) {{
        {fn}("[desktop_qt] G1 thin QML not ready\\n");
        return;
    }}
    {fn}("[desktop_qt] G1 thin QML Ready\\n");
    QObject *obj = g_g1_comp->beginCreate(g_engine->rootContext());
    if (!obj) {{
        {fn}("[desktop_qt] G1 beginCreate null\\n");
        return;
    }}
    g_g1_comp->completeCreate();
    {fn}("[desktop_qt] G1 beginCreate ok\\n");
    if (qobject_cast<QQuickWindow *>(obj) || qobject_cast<QWindow *>(obj))
        {fn}("[desktop_qt] G1 QML root is QWindow\\n");
    else if (qobject_cast<QQuickItem *>(obj))
        {fn}("[desktop_qt] G1 QML root is QQuickItem\\n");
    {fn}("[desktop_qt] G1 product QML Ready\\n");
}}

'''


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    if "G1 post-loop thin QML begin" in src and "guest_g1_post_loop_thin_qml();" in src:
        print("already patched:", path)
        return

    fn = serial_fn(src)
    if "guest_g1_post_loop_thin_qml" not in src:
        anchor = re.search(
            r"^static void guest_gate1_window_controls_probe\(void\)",
            src,
            re.M,
        )
        if not anchor:
            raise SystemExit("guest_gate1_window_controls_probe not found")
        src = src[: anchor.start()] + funcs(fn) + src[anchor.start() :]

    if "guest_g1_post_loop_thin_qml();" not in src:
        loop = re.search(
            r"^([ \t]*)for \(\;\;\) \{[ \t]*\n",
            src[src.find("entering event loop") if "entering event loop" in src else 0 :],
            re.M,
        )
        if not loop:
            loop = re.search(r"^([ \t]*)for \(\;\;\) \{[ \t]*\n", src, re.M)
        if not loop:
            raise SystemExit("event loop for(;;) not found")
        # search() above may be on a slice — recompute on full src after enter-loop
        start = src.find("entering event loop")
        if start < 0:
            start = 0
        loop = re.search(r"^([ \t]*)for \(\;\;\) \{[ \t]*\n", src[start:], re.M)
        if not loop:
            raise SystemExit("event loop for(;;) after entering event loop not found")
        abs_pos = start + loop.end()
        indent = loop.group(1) + "    "
        insert = (
            f"{indent}guest_g1_post_loop_thin_qml();\n"
            f"{indent}guest_g1_post_loop_thin_qml_poll();\n"
        )
        src = src[:abs_pos] + insert + src[abs_pos:]

    bak = path.with_suffix(path.suffix + ".bak-g1")
    if not bak.exists():
        bak.write_text(path.read_text(encoding="utf-8", errors="replace"), encoding="utf-8")
    path.write_text(src, encoding="utf-8")
    print("patched:", path)
    print("backup:", bak)
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if "G1 post-loop thin QML begin" in line or "guest_g1_post_loop_thin_qml();" in line:
            print(f"  {i}:{line}")


if __name__ == "__main__":
    main()
